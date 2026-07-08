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
    move-op lifecycle (t/s/a/c/f), and the per-block expiry + kill hooks.  The
    only per-type code is a JobPredicate object (see TransportPredicate); the
    rest is type-independent.

    Confirmed-only: job moves are processed only in the confirmed block path
    (MoveProcessor), never in the pending path (PendingStateUpdater does not
    dispatch the "j" key), so it is safe to read ctx.Timestamp() here.  If jobs
    are ever wired into pending, the timestamp-using guards must be revisited.
*/

#include "context.hpp"

#include "database/account.hpp"
#include "database/building.hpp"
#include "database/character.hpp"
#include "database/inventory.hpp"
#include "database/jobs.hpp"
#include "database/ongoing.hpp"

#include <json/json.h>

#include <glog/logging.h>

#include <memory>

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
  BuildingInventoriesTable& buildingInv;
  CharacterTable& characters;
  OngoingsTable& ongoings;
  JobsTable& jobs;
};

/**
 * Returns true if the given building may be a delivery destination for a job
 * of the given faction: same faction or a neutral (ancient) building.  Enemy
 * safe zones are impassable to other factions, so a cross-faction destination
 * would be undeliverable.
 */
bool IsSameOrNeutralDestination (const Building& b, Faction f);

/* ************************************************************************** */
/* The per-type predicate interface.                                          */

/** Outcome of applying a fulfil op.  */
enum class FulfilResult
{
  /** Progress was recorded (proto mutated); the job stays open.  */
  PROGRESS,
  /** The job is satisfied; the generic op pays out and deletes the row.  */
  COMPLETE,
};

/**
 * The one per-type object.  A job type implements this; everything else
 * (escrow, the t/s/a/c/f dispatch, assign/accept/cancel, the expiry and kill
 * sweeps, reserved-balance sums) is the shared core.  Adding a type = adding
 * one predicate object and registering it in PredicateForType.
 */
class JobPredicate
{

public:

  virtual ~JobPredicate () = default;

  /**
   * Whether accepting this type requires the poster to have designated the
   * worker up front (the approval-required types: protect / destroy / escort).
   */
  virtual bool
  RequiresApproval () const
  {
    return false;
  }

  /**
   * Validates the type-specific terms of a POST move (no state change).  The
   * generic reward / collateral / deadline / affordability checks are done by
   * the caller; this only checks the type terms.  Returns false to reject.
   */
  virtual bool ValidatePost (const JobContext& jc, const Account& poster,
                             const Json::Value& terms) const = 0;

  /**
   * Applies the (already-validated) terms to the freshly-created OPEN job:
   * fills the linked entity, any audience-faction override and the proto
   * payload.  Called from POST after the generic escrow + row creation.
   */
  virtual void ApplyPost (const JobContext& jc, const Account& poster,
                          const Json::Value& terms, Job& job) const = 0;

  /**
   * Checks whether `worker` can fulfil `job` with `args` (no state change).
   * Called from the fulfil op's validation.
   */
  virtual bool CanFulfil (const JobContext& jc, const Job& job,
                          const Account& worker,
                          const Json::Value& args) const = 0;

  /**
   * Performs the fulfil: moves the type-specific goods and settles the coins.
   * Returns COMPLETE (the generic op then deletes the row) or PROGRESS (the
   * row stays, with the mutated proto persisted).  Must not delete the row.
   */
  virtual FulfilResult DoFulfil (const JobContext& jc, Job& job,
                                 Account& worker,
                                 const Json::Value& args) const = 0;

  /**
   * Settles a job whose deadline has passed (confirmed block only).  Handles
   * both the OPEN case (always void + refund the poster) and the ACCEPTED
   * case (type-specific: transport fails and forfeits collateral to the
   * poster; success-on-expiry types pay the worker).  The caller deletes the
   * row afterwards.
   */
  virtual void OnExpire (const JobContext& jc, Job& job) const = 0;

  /**
   * Settles a job whose linked entity was destroyed (confirmed block only).
   * The caller deletes the row afterwards.
   */
  virtual void OnLinkedEntityDestroyed (const JobContext& jc,
                                        Job& job) const = 0;

};

/**
 * Returns the predicate object for a job type, or nullptr for an unknown type.
 * The returned reference is to a process-lifetime singleton.
 */
const JobPredicate* PredicateForType (Job::Type type);

/** Maps a POST move type name ("transport", ...) to a Job::Type.  */
Job::Type JobTypeFromString (const std::string& name);

/* ************************************************************************** */
/* The generic move-op lifecycle.                                             */

/**
 * A single jobs-board operation parsed from one element of the "j" array
 * (post / assign / accept / cancel / fulfil).  Mirrors the DexOperation family:
 * the lifecycle is generic and the type-specific parts delegate to the job's
 * JobPredicate.
 */
class JobOperation
{

protected:

  JobContext jc;

  /** The account triggering the operation.  */
  Account& account;

  /** The operation's raw move JSON (for logs and error reporting).  */
  Json::Value rawMove;

  JobOperation (Account& a, const JobContext& c)
    : jc(c), account(a)
  {}

public:

  virtual ~JobOperation () = default;

  const Account&
  GetAccount () const
  {
    return account;
  }

  /** Returns true if the operation is valid per game and move rules.  */
  virtual bool IsValid () const = 0;

  /** Returns the pending-JSON representation of this operation.  */
  virtual Json::Value ToPendingJson () const = 0;

  /** Fully executes the update corresponding to this operation.  */
  virtual void Execute () = 0;

  /**
   * Tries to parse a job operation from one "j"-array element.  Returns
   * nullptr if the format is invalid (unknown or non-unique t/s/a/c/f
   * discriminator, malformed fields).
   */
  static std::unique_ptr<JobOperation> Parse (Account& acc,
                                              const Json::Value& data,
                                              const JobContext& jc);

};

/* ************************************************************************** */
/* Per-block hooks (confirmed processing only).                               */

/**
 * Expires all non-standing jobs whose deadline has been reached at the current
 * block timestamp, running each type's OnExpire settlement and deleting the
 * rows.  Called once per block; on the (vast majority of) blocks where nothing
 * is due it is an indexed no-op that touches no rows.
 */
void ExpireJobs (Database& db, const Context& ctx);

/**
 * Handles all jobs linked to a building that is being destroyed, running each
 * type's OnLinkedEntityDestroyed settlement and deleting the rows.  Must be
 * called from the combat kill-processor while the building row still exists.
 * Runs only on the rare event of a building death.
 */
void OnBuildingDestroyed (Database& db, const Context& ctx,
                          Database::IdT buildingId);

} // namespace pxd

#endif // PXD_JOBS_HPP
