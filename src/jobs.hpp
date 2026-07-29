/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef PXD_JOBS_HPP
#define PXD_JOBS_HPP

/*
    The jobs board: an on-chain escrow-deal system.  A job is an escrowed
    reward + a worker collateral + a deadline, optionally exclusive to one
    designated worker, settled by a completion percentage that a bound arbiter
    dials on a dispute.  This module holds the whole subsystem: the coin
    escrow, the settlement math, the move-op lifecycle (post/assign/accept/
    cancel + the deal ops) and the superblock expiry hook.

    The deal is deliberately generic: it subsumes what would otherwise be a
    catalogue of per-type verification jobs (transport, haul, escort, rental,
    ...) as a cosmetic type tag rather than bespoke consensus code -- so the
    chain never has to adjudicate a delivery it cannot observe.

    Confirmed-only: job moves are processed only in the confirmed block path
    (MoveProcessor), never in the pending path (PendingStateUpdater does not
    dispatch the "j" key), so it is safe to read ctx.Timestamp() here.  If jobs
    are ever wired into pending, the timestamp-using guards must be revisited.
*/

#include "context.hpp"

#include "database/account.hpp"
#include "database/jobs.hpp"
#include "database/params.hpp"

#include <json/json.h>

#include <glog/logging.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace pxd
{

/* ************************************************************************** */
/* Coin escrow.                                                               */

/*
   Job settlement moves vCHI directly on account balances and is deliberately
   FEE-FREE: it must never route through the DEX's PayToSellerAndFee (which
   skims the trading fee + burn), or a returned collateral would be shaved.
   Locked coins are not held in any account; they live in the jobs table's
   reward/collateral columns and are surfaced in balance.reserved.
*/

/**
 * Locks coins by removing them from an account's balance (the poster's reward
 * at post time, the worker's collateral at accept time).  The caller must have
 * verified sufficient balance first (AddBalance CHECK-fails on overdraft).
 */
inline void
LockJobCoins (Account& a, const Amount amount)
{
  CHECK_GE (amount, 0);
  a.AddBalance (-amount);
}

/**
 * Releases locked coins back onto an account's balance: a reward payout, a
 * collateral return, or a refund.  Fee-free by construction.
 */
inline void
ReleaseJobCoins (Account& a, const Amount amount)
{
  CHECK_GE (amount, 0);
  a.AddBalance (amount);
}

/* ************************************************************************** */
/* Escrow-deal settlement (spec escrow-deal-system-design.md §6.3).           */

/**
 * The split of a settled escrow deal: what the four parties receive.  The sum
 * is EXACTLY the escrow (reward + collateral); the treasury tax is burned until
 * a faction treasury exists to receive it (a Phase-4 wiring).
 */
struct DealSettlement
{
  Amount worker;
  Amount poster;
  Amount arbiter;
  Amount treasury;
};

/**
 * Computes the escrow-deal settlement at completion percentage p in [0,100]:
 * tax and fee fall only on the TRANSACTED value (reward + seized collateral),
 * never on the worker's returned stake, split between the parties in proportion
 * to the share each receives, with the remainder pinned to the poster so that
 * `worker + poster + arbiter + treasury == reward + collateral` EXACTLY for all
 * inputs (brute-force proven: 0 conservation failures, 0 negatives, endpoints
 * exact).  taxBps/feeBps are basis points with taxBps + feeBps < 10000.
 */
DealSettlement ComputeDealSettlement (Amount reward, Amount collateral, int p,
                                      int taxBps, int feeBps);

/* ************************************************************************** */
/* Shared context + helpers.                                                  */

/**
 * The bundle of database-table handles plus the processing Context that job
 * operations and the block hooks all need.  It holds references,
 * so the underlying tables must outlive it (they are the MoveProcessor's
 * long-lived handles, or locally-constructed ones in the block hooks).
 */
struct JobContext
{
  const Context& ctx;
  AccountsTable& accounts;
  JobsTable& jobs;
  /** Runtime-parameter reads (admission caps, floors, deal economics).  */
  const ParamsTable& params;
};

/* Immutable admission-cap maxima (design escrow-v1.1 §4).  The runtime "param"
   command stores unbounded int64s, so every consensus read of a
   liveness-bounding param clamps the ParamsTable overlay to a compile-time
   ceiling: a fat-fingered or compromised admin key can tighten a cap (or freeze
   an admission with 0) but never open an unbounded-sweep hole above these
   values.  A negative override clamps to the floor 0 (a freeze), matching the
   existing 0=freeze semantics.  The getjobsparams RPC reports the SAME clamped
   value, so the client previews against exactly what consensus uses.

   EVIDENCE STATUS: the ceilings are headroom bounds, NOT benched limits.  The
   only settling-block timings we hold (~0.105s at 11x the 10k default) were
   measured on the superseded jobs-superblocks branch, whose job-type set
   differs from this one, and no in-tree harness reproduces them here: the
   largest ordinary sweep test is 200 rows.  Until an ExpireJobs benchmark runs
   at CAP_MAX_LIVE_JOBS through full block wiring under a stated block-time
   budget, keep max-live-jobs at its conservative 10k roconfig default and
   treat the headroom above it as unproven.  */
constexpr int64_t CAP_MAX_LIVE_JOBS = 100000;
constexpr int64_t CAP_MAX_JOBS_PER_POSTER = 2000;
/** Ceiling on the snapshotted deal reaction window (30 days).  */
constexpr int64_t CAP_DEAL_REACTION_WINDOW = 2592000;

/**
 * Reads a runtime param (override over its roconfig default) and clamps it to
 * [floor, ceiling] -- the ONE path every consensus read of a capped param and
 * the getjobsparams RPC share, so no second unclamped path can ever exist.
 */
inline int64_t
CappedParam (const ParamsTable& params, const std::string& name,
             const int64_t rocoDefault, const int64_t ceiling,
             const int64_t floor = 0)
{
  return std::clamp<int64_t> (params.Get (name, rocoDefault), floor, ceiling);
}

/**
 * How a deal left the board.  Not persisted: it selects settlement BEHAVIOUR
 * -- whether the bound arbiter is credited with a ruling, and whether the two
 * parties' dispute counters are bumped.
 */
enum class DealSettleMode
{
  /** Both parties confirmed: released at p=100.  */
  BOTH_CONFIRM,
  /** The bound arbiter ruled the given p.  */
  RULING,
  /** End-date sweep with exactly one confirm: p=100.  */
  SINGLE_CONFIRM,
  /** Sweep on an unruled dispute: p=50, fee forfeited.  */
  GHOST_SPLIT,
};

/**
 * Settles an ACCEPTED escrow deal at completion percentage p: releases the
 * §6.3 split to the worker, poster and (if any) arbiter, burns the treasury
 * tax, and bumps the worker's DEAL reputation counters (value-gated: only when
 * p>0 and a real tax was burned).  `executor` is the move's account handle
 * when settling inside a move op (confirm / rule) -- credited through it to
 * avoid a second live handle on the same row; it is nullptr in the block-hook
 * (expiry) path where no account handle is live.  Credits are accumulated per
 * account name so poster==arbiter or a self-arbiter never double-opens a row.
 * `payArbiterFee` is false only on the ghosted-dispute timeout, where the
 * arbiter FORFEITS its fee (each party keeps its own fee share): paying the
 * full fee for ignoring the one dispute it was hired to rule would make
 * ghosting strictly better than ruling.
 */
void SettleDeal (const JobContext& jc, Job& job, int p, Account* executor,
                 bool payArbiterFee, DealSettleMode mode);

/**
 * Refunds both stakes of an ACCEPTED deal (worker <- collateral, poster <-
 * reward): the neither-party-acted timeout.  Hook-path only (no live account
 * handles).
 */
void RefundBothDeal (const JobContext& jc, Job& job);

/**
 * Settles a job whose deadline has passed (confirmed block only).  Handles
 * both the OPEN case (void + refund the poster) and the ACCEPTED case (the
 * deal resolves confirm-aware).  The caller deletes the row afterwards.
 */
void ExpireJob (const JobContext& jc, Job& job);

/* ************************************************************************** */
/* The move-op lifecycle.                                                     */

/**
 * A single jobs-board operation parsed from one element of the "j" array
 * (post / assign / accept / cancel / deal action).  Mirrors the DexOperation
 * family.
 */
class JobOperation
{

protected:

  JobContext jc;

  /** The account triggering the operation.  */
  Account& account;

  JobOperation (Account& a, const JobContext& c)
    : jc(c), account(a)
  {}

public:

  virtual ~JobOperation () = default;

  /** Returns true if the operation is valid per game and move rules.  */
  virtual bool IsValid () const = 0;

  /** Fully executes the update corresponding to this operation.  */
  virtual void Execute () = 0;

  /**
   * Tries to parse a job operation from one "j"-array element.  Returns
   * nullptr if the format is invalid (unknown or non-unique t/s/a/c/dl
   * discriminator, malformed fields).
   */
  static std::unique_ptr<JobOperation> Parse (Account& acc,
                                              const Json::Value& data,
                                              const JobContext& jc);

};

/* ************************************************************************** */
/* Superblock hooks (confirmed processing only).                              */

/**
 * Expires all jobs whose deadline has been reached at the current
 * block timestamp, running each type's OnExpire settlement and deleting the
 * rows.  Called once per SUPERBLOCK (a deadline passing on an ordinary block
 * settles at the next superblock sweep), AFTER kill processing (the normative
 * phase order: an entity dying in the boundary superblock is a death, not a
 * survival); on the (vast majority of) sweeps where nothing is due it is an
 * indexed no-op that touches no rows.
 *
 * The sweep itself is deliberately uncapped (no continuation across
 * blocks): a deterministic sweep cap would defer settlement of already-due
 * jobs to later blocks, re-opening the very window (mutable inputs after
 * the deadline) that JobIsDue exists to close.  The hard bound lives at the
 * DOOR instead: the admission caps in PostOperation::IsValid (max live
 * jobs in total / per poster,
 * admin-tunable via the "param" command) mean no sweep, entity cascade
 * or payout can ever exceed the capped board -- every due row inside it
 * was paid for (posting fee burned, escrow locked), and settlement is a
 * constant amount of work per job walked straight off the (deadline, id)
 * index with no sort.
 *
 * Should a bound ever be demanded by measurement, the designated mechanism
 * is ADMISSION control (tightening the caps above), which bounds every
 * future cohort at posting time without touching the settlement semantics
 * of rows already on the board -- never a sweep cap.
 */
void ExpireJobs (Database& db, const Context& ctx);

/**
 * Validates jobs-table invariants as part of ValidateStateSlow: poster and
 * worker accounts exist and match the status, and every job has a deadline.
 */
void ValidateJobs (Database& db);

} // namespace pxd

#endif // PXD_JOBS_HPP
