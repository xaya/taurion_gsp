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
    The jobs board: a single generic on-chain contracts system.  A job is an
    escrowed reward + a completion predicate + a deadline, optionally exclusive
    to one worker who locks a collateral bond.  This module holds the whole
    subsystem: the coin escrow, the per-type predicate interface, the generic
    move-op lifecycle (t/s/a/c + the deal ops), and the superblock expiry + kill
    hooks.  The only per-type code is a JobPredicate object; the rest is
    type-independent.

    The catalogue carried on this one core: the standing wanted board
    (kill-hook settled), ad-slot rentals on a building, and the generic
    escrow deal -- the arbiter-settled reward+collateral primitive that
    subsumes the rest (transport, haul, escort, rental, ...) as a cosmetic
    type tag rather than bespoke consensus code.

    Confirmed-only: job moves are processed only in the confirmed block path
    (MoveProcessor), never in the pending path (PendingStateUpdater does not
    dispatch the "j" key), so it is safe to read ctx.Timestamp() here.  If jobs
    are ever wired into pending, the timestamp-using guards must be revisited.
*/

#include "context.hpp"

#include "database/account.hpp"
#include "database/building.hpp"
#include "database/character.hpp"
#include "database/damagelists.hpp"
#include "database/jobs.hpp"
#include "database/params.hpp"

#include "proto/combat.pb.h"

#include <json/json.h>

#include <glog/logging.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

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
 * operations, predicates and the block hooks all need.  It holds references,
 * so the underlying tables must outlive it (they are the MoveProcessor's
 * long-lived handles, or locally-constructed ones in the block hooks).
 */
struct JobContext
{
  const Context& ctx;
  AccountsTable& accounts;
  BuildingsTable& buildings;
  JobsTable& jobs;
  /** Runtime-parameter reads (admission caps, floors, deal economics).  */
  const ParamsTable& params;
};

/**
 * Settles an ACCEPTED escrow deal at completion percentage p: releases the
 * §6.3 split to the worker, poster and (if any) arbiter, burns the treasury
 * tax, and bumps the worker's DEAL reputation counters (value-gated: only when
 * p>0 and a real tax was burned).  Returns the history outcome (COMPLETED for
 * p>0, FAILED for p==0).  `executor` is the move's account handle when settling
 * inside a move op (confirm / rule) -- that party is credited through it to
 * avoid a second live handle on the same row; it is nullptr in the block-hook
 * (expiry) path where no account handle is live.  Credits are accumulated per
 * account name so poster==arbiter or a self-arbiter never double-opens a row.
 * `payArbiterFee` is false only on the ghosted-dispute timeout, where the
 * arbiter FORFEITS its fee (each party keeps its own fee share): paying the
 * full fee for ignoring the one dispute it was hired to rule would make
 * ghosting strictly better than ruling.
 */
JobOutcome SettleDeal (const JobContext& jc, const Job& job, int p,
                       Account* executor, bool payArbiterFee);

/**
 * Refunds both stakes of an ACCEPTED deal (worker <- collateral, poster <-
 * reward): the neither-party-acted timeout.  Returns VOID.  Hook-path only
 * (no live account handles).
 */
JobOutcome RefundBothDeal (const JobContext& jc, const Job& job);

/* ************************************************************************** */
/* The per-type predicate interface.                                          */

/**
 * What kind of entity a job type stores in linked_id.  This is a predicate
 * property so the state validator and the JSON rendering derive it from the
 * one per-type object instead of keeping parallel type switches in sync.
 */
enum class JobLinkedKind
{
  NONE,
  BUILDING,
};

/**
 * The one per-type object.  A job type implements this; everything else
 * (escrow, the t/s/a/c + deal-op dispatch, assign/accept/cancel, the expiry and
 * kill sweeps, reserved-balance sums, completion counters) is the shared core.
 * Adding a type = adding one predicate object and registering it in
 * PredicateForType.
 */
class JobPredicate
{

public:

  virtual ~JobPredicate () = default;

  /**
   * Whether accepting this type requires the poster to have designated the
   * worker up front -- the ad-slot, whose designated worker is the building
   * owner (their accept is the content approval).
   */
  virtual bool
  RequiresApproval () const
  {
    return false;
  }

  /**
   * Whether this is the standing duration class: posted without a deadline
   * and never swept, with the notice-based cancel as its only exit.  Only
   * the wanted board is standing; no other type may be.
   */
  virtual bool
  IsStanding () const
  {
    return false;
  }

  /**
   * The audience faction stored on the job row: who may accept (and whose
   * board shows it).  Defaults to the poster's faction; the open types
   * (wanted, ad-slot) override to INVALID = all factions.
   */
  virtual Faction
  AudienceFaction (const Account& poster) const
  {
    return poster.GetFaction ();
  }

  /**
   * What kind of entity this type stores in linked_id (NONE if it never
   * links one).  Types that set linked_id in ApplyPost must override this.
   */
  virtual JobLinkedKind
  LinkedEntityKind () const
  {
    return JobLinkedKind::NONE;
  }

  /**
   * Whether this type settles through the character-kill hook (OnTargetKill).
   * Gates the kill attribution, so a future type reusing the linked_name
   * column for another purpose is never swept into a settlement path it
   * does not implement.
   */
  virtual bool
  SettlesOnTargetKill () const
  {
    return false;
  }

  /**
   * The type-specific POST term keys this predicate reads, beyond the
   * generic t / d / r / co handled by the caller.  POST parsing rejects
   * a move containing any other member, so the POST grammar is exactly as
   * strict as the lifecycle ops' (typos surface as rejections instead of
   * being silently ignored).  Presence/value rules (e.g. standing types
   * must omit "d") remain in the generic and per-type validation.
   * Returned by reference to a static so the hot parse path allocates
   * nothing.
   */
  virtual const std::vector<std::string>& PostTermKeys () const = 0;

  /**
   * The term keys holding the entity IDs a POST of this type would link
   * (empty for non-linking types), for the per-linked-entity admission cap.
   * Returned by reference to a static, like PostTermKeys.
   */
  virtual const std::vector<const char*>&
  PostLinkedIdKeys () const
  {
    static const std::vector<const char*> none;
    return none;
  }

  /**
   * The account name a POST of this type would store in linked_name (empty
   * for types that target no name), for the per-target stacked-listings
   * admission cap -- derived from the terms like PostLinkedIds, so the
   * generic cap check never reaches into a type's private grammar.  Called
   * only with terms that already passed ValidatePost.
   */
  virtual std::string
  PostLinkedName (const Json::Value& terms) const
  {
    return "";
  }

  /**
   * The minimum reward a POST of this type must escrow, beyond the global
   * "min-job-reward" floor (the larger of the two applies).  Wanted pools
   * demand the higher "min-bounty-reward", so occupying one of a target's
   * capped pool slots always locks real value.
   */
  virtual Amount
  MinReward (const JobContext& jc) const
  {
    return 0;
  }

  /**
   * Parses the IDs named by PostLinkedIdKeys out of the terms, for the
   * per-linked-entity admission cap (checked before any state change;
   * ApplyPost stores the actual link).  Called only with terms that already
   * passed ValidatePost, so the IDs are known to parse.
   */
  std::vector<Database::IdT> PostLinkedIds (const Json::Value& terms) const;

  /**
   * Validates the type-specific terms of a POST move (no state change).  The
   * generic reward / collateral / deadline / affordability checks are done by
   * the caller; this only checks the type terms.  Returns false to reject.
   */
  virtual bool ValidatePost (const JobContext& jc, const Account& poster,
                             const Json::Value& terms) const = 0;

  /**
   * Applies the (already-validated) terms to the freshly-created OPEN job:
   * fills the linked entity / target name, the designated worker (the ad-slot
   * building owner), and the proto payload.  Called from POST after the
   * generic escrow + row creation.
   */
  virtual void ApplyPost (const JobContext& jc, Account& poster,
                          const Json::Value& terms, Job& job) const = 0;

  /**
   * Type-specific extra validation of an ACCEPT (no state change), on top of
   * the generic audience / designation / runway / collateral checks.  The
   * open-claim types return false unconditionally (they have no accept step);
   * ad-slot checks that the accepting worker still owns the building.
   */
  virtual bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const
  {
    return true;
  }

  /**
   * Settles a job whose deadline has passed (confirmed block only).  Handles
   * both the OPEN case (always void + refund the poster) and the ACCEPTED
   * case (type-specific: the ad-slot pays the worker its rent at the window's
   * end; the deal resolves confirm-aware).  Kills are processed before the
   * expiry sweep
   * within a block, so "alive at expiry" is well-defined.  Returns the
   * outcome the caller records in the job_history table before deleting
   * the row.
   */
  virtual JobOutcome OnExpire (const JobContext& jc, Job& job) const = 0;

  /**
   * Settles a job whose linked building was destroyed (confirmed block
   * only).  Returns the outcome the caller records in the job_history
   * table before deleting the row.  Types that never set linked_id keep
   * this default, which must never be reached.
   */
  virtual JobOutcome
  OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const
  {
    LOG (FATAL) << "Job " << job.GetId () << " has no linked entity";
  }

  /**
   * Settles a job whose linked building changed owner by a sale (b.send).
   * Returns true when the job is settled (the caller records VOID in the
   * job_history table and deletes the row) and false when the job is
   * unaffected by the transfer (the default).  Only the ad-slot type reacts,
   * voiding + refunding the advertiser -- the
   * new owner never approved the content and the old owner is no longer the
   * payee.
   */
  virtual bool
  OnLinkedBuildingTransferred (const JobContext& jc, Job& job) const
  {
    /* The caller (the move processor's building transfer) still holds a live
       handle to the transferred building, so an override must not open its
       own handle to that building (it would collide on the unique-handle
       tracker).  Account handles are fine -- the recipient's is released
       before this runs.  */
    return false;
  }

  /**
   * Consumes one qualifying kill on a linked_name job: one tranche leaves
   * the escrow.  A wanted pool touches NO accounts here -- the per-owner
   * equal split is returned through `sharePerOwner` (0 when the tranche
   * cannot give every killer at least one coin) and the caller pays each
   * owner ONCE with the total across all pools (PayKillShares), so account
   * work per kill is O(pools + owners), never O(pools x owners).  Returns
   * the settlement outcome once the pool is finished (DRAINED / COMPLETED;
   * the caller records it and deletes the row), or INVALID while the job
   * stays on the board.  Only the wanted type implements this.
   */
  virtual JobOutcome
  OnTargetKill (const JobContext& jc, Job& job,
                const std::set<std::string>& killOwners,
                Amount& sharePerOwner) const
  {
    LOG (FATAL)
        << "Job " << job.GetId () << " has no target-kill settlement";
  }

};

/**
 * Pays the aggregated wanted-pool kill shares: each distinct killer account
 * is opened once and credited the total share accumulated across every
 * paying pool, with its completion counters bumped once per paying pool.
 * The end state is exactly the per-pool payout's, at O(owners) account
 * writes.  Must not be called while any account handle is live.
 */
void PayKillShares (const JobContext& jc, const std::set<std::string>& owners,
                    unsigned payingPools, Amount totalShare);

/**
 * Returns the predicate object for a job type, or nullptr for an unknown type.
 * The returned reference is to a process-lifetime singleton.
 */
const JobPredicate* PredicateForType (Job::Type type);

/** Maps a POST move type name ("wanted", ...) to a Job::Type.  */
Job::Type JobTypeFromString (const std::string& name);

/** Returns the move/JSON name for a job type, or nullptr if unknown.  */
const char* JobTypeName (Job::Type type);

/** Returns the JSON name for a history outcome, or nullptr if unknown.  */
const char* JobOutcomeName (JobOutcome outcome);

/* ************************************************************************** */
/* The generic move-op lifecycle.                                             */

/**
 * A single jobs-board operation parsed from one element of the "j" array
 * (post / assign / accept / cancel).  Mirrors the DexOperation family:
 * the lifecycle is generic and the type-specific parts delegate to the job's
 * JobPredicate.
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
 * Expires all non-standing jobs whose deadline has been reached at the current
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
 * jobs in total / per poster / per linked entity / per bounty target,
 * admin-tunable via the "param" command) mean no sweep, entity cascade
 * or payout can ever exceed the capped board -- every due row inside it
 * was paid for (posting fee burned, escrow locked), and settlement is a
 * constant amount of work per job walked straight off the (deadline, id)
 * index with no sort.  The history prune is batched separately
 * (params.jobs_history_prune_batch).
 *
 * Should a bound ever be demanded by measurement, the designated mechanism
 * is ADMISSION control (tightening the caps above), which bounds every
 * future cohort at posting time without touching the settlement semantics
 * of rows already on the board -- never a sweep cap.
 */
void ExpireJobs (Database& db, const Context& ctx);

/**
 * Handles all jobs linked to a building that is being destroyed (the ad
 * slots -- buildings are the only entity kind any type links), running each
 * type's OnLinkedEntityDestroyed settlement and deleting the rows.  Must be
 * called from the combat kill-processor while the building row still
 * exists.  Runs only on the rare event of an actual death, and is an
 * indexed no-op when no job is linked to the building.
 */
void OnJobEntityDestroyed (Database& db, const Context& ctx,
                           Database::IdT entityId);

/**
 * Handles all jobs linked to a building whose ownership was transferred by
 * a move (b.send), running each type's OnLinkedBuildingTransferred hook and
 * recording + deleting the rows it settles.  Must be called from the move
 * processor right after the owner change.  Indexed no-op when no job is
 * linked to the building.
 */
void OnJobBuildingTransferred (Database& db, const Context& ctx,
                               Database::IdT buildingId);

/**
 * Kill-time resolver for the wanted bounty pools: pays tranches for
 * qualifying character kills.  Constructed once per superblock alongside the
 * FameUpdater and fed every kill from the same pre-removal pass (damage lists
 * and victim rows still live), sharing fame's distinct-owner derivation
 * semantics.  The work is proportional to the DEATHS, never to the dormant
 * board: the constructor makes a single covering-index any-bounty-at-all
 * probe, and each death costs at most one indexed linked-name probe --
 * bounty-free owners are memoised after their first empty probe, while an
 * owner with live pools is re-probed per death (the pools drain as they
 * pay).  Arbitrarily many dormant bounty targets thus add nothing to an
 * unrelated death -- the mega-battle perf guard.
 */
class JobsBountyTracker
{

private:

  Database& db;
  const Context& ctx;

  /** The superblock's damage lists (owned by the FameUpdater).  */
  const DamageLists& dl;

  /** Character table for owner lookups on still-live rows.  */
  CharacterTable characters;

  /** Whether any bounty exists at all (one covering-index probe).  */
  bool anyBounties;

  /**
   * Owners already probed and found bounty-free.  Safe to memoise within the
   * superblock: moves precede hooks, so pools can only shrink (drain) during
   * the kill pass, never appear.
   */
  std::set<std::string> noBounty;

public:

  explicit JobsBountyTracker (Database& d, const Context& c,
                              const DamageLists& l);

  JobsBountyTracker () = delete;
  JobsBountyTracker (const JobsBountyTracker&) = delete;
  void operator= (const JobsBountyTracker&) = delete;

  /**
   * Processes one killed fighter: if it is a character whose owner is under
   * a wanted bounty, pays one tranche per pool on that name -- split across
   * the distinct accounts on the victim's damage list.  Kills with an empty
   * owner set (e.g. turret-only damage) pay nothing and consume nothing.
   */
  void UpdateForKill (const proto::TargetId& target);

};

/**
 * Validates jobs-table invariants as part of ValidateStateSlow: poster /
 * worker accounts exist and match the status, linked entities exist, and
 * only standing types lack a deadline.
 */
void ValidateJobs (Database& db);

} // namespace pxd

#endif // PXD_JOBS_HPP
