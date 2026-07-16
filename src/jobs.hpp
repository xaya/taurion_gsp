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
    only per-type code is a JobPredicate object; the rest is type-independent.

    The catalogue carried on this one core: transport / haul /
    construction-supply (delivery), the standing wanted board, protect and
    destroy building, escort / bodyguard / patrol, rentals of packaged items,
    and ad-slot / toll rentals where the poster is the payer.

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
#include "database/inventory.hpp"
#include "database/jobs.hpp"
#include "database/ongoing.hpp"

#include "proto/combat.pb.h"

#include <json/json.h>

#include <glog/logging.h>

#include <memory>
#include <set>
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
  GroundLootTable& groundLoot;
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
 * What kind of entity a job type stores in linked_id.  This is a predicate
 * property so the state validator and the JSON rendering derive it from the
 * one per-type object instead of keeping parallel type switches in sync.
 */
enum class JobLinkedKind
{
  NONE,
  BUILDING,
  CHARACTER,
};

/**
 * The one per-type object.  A job type implements this; everything else
 * (escrow, the t/s/a/c/f dispatch, assign/accept/cancel, the expiry and kill
 * sweeps, reserved-balance sums, completion counters) is the shared core.
 * Adding a type = adding one predicate object and registering it in
 * PredicateForType.
 */
class JobPredicate
{

public:

  virtual ~JobPredicate () = default;

  /**
   * Whether accepting this type requires the poster to have designated the
   * worker up front: the approval types (protect / destroy / escort /
   * bodyguard) and the payee types where the designated worker IS the
   * counterparty (rental / ad-slot / toll).
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
   * Whether jobs of this type carry a work window ("wd"): at ACCEPT the
   * deadline is rewritten to the accept timestamp plus the window, so the
   * worker always gets the full window from the moment they commit.  True
   * for every deadlined type except ad-slots, which rent a calendar window
   * (their posted deadline IS the window end) and override this to false;
   * the standing class has no deadline to rewrite.
   */
  virtual bool
  UsesWorkWindow () const
  {
    return !IsStanding ();
  }

  /**
   * Whether the FULFIL op is submitted by the poster rather than the worker.
   * True only for rentals, where the poster (renter) returns the goods and
   * the designated worker (lessor) is the payee.
   */
  virtual bool
  PosterFulfils () const
  {
    return false;
  }

  /**
   * The audience faction stored on the job row: who may accept / fulfil (and
   * whose board shows it).  Defaults to the poster's faction; the open types
   * (wanted, destroy, ad-slot, toll) override to INVALID = all factions.
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
   * Validates the type-specific terms of a POST move (no state change).  The
   * generic reward / collateral / deadline / affordability checks are done by
   * the caller; this only checks the type terms.  Returns false to reject.
   */
  virtual bool ValidatePost (const JobContext& jc, const Account& poster,
                             const Json::Value& terms) const = 0;

  /**
   * Applies the (already-validated) terms to the freshly-created OPEN job:
   * fills the linked entity / target name, the designated worker for the
   * payee types, and the proto payload.  May also move goods (haul reserves
   * the manifest out of the poster's inventory here).  Called from POST after
   * the generic escrow + row creation.
   */
  virtual void ApplyPost (const JobContext& jc, Account& poster,
                          const Json::Value& terms, Job& job) const = 0;

  /**
   * Type-specific extra validation of an ACCEPT (no state change), on top of
   * the generic audience / designation / runway / collateral checks.  The
   * open-claim types return false unconditionally (they have no accept step);
   * rental verifies the lessor holds the goods; ad-slot that the accepting
   * worker still owns the building.
   */
  virtual bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const
  {
    return true;
  }

  /**
   * Type-specific side effect of a successful ACCEPT, after the generic
   * collateral lock and status change.  Rental moves the rented items from
   * the lessor to the renter; haul hands the reserved goods to the worker and
   * re-links the job from the source to the destination building.
   */
  virtual void
  OnAccept (const JobContext& jc, Job& job, Account& worker) const
  {}

  /**
   * Type-specific side effect of a (non-standing) CANCEL, before the generic
   * reward refund and row deletion.  Haul returns the reserved goods to the
   * poster here.
   */
  virtual void
  OnCancel (const JobContext& jc, Job& job) const
  {}

  /**
   * Checks whether the executor can fulfil `job` with `args` (no state
   * change).  The executor is the worker, or the poster for PosterFulfils
   * types.  Called from the fulfil op's validation; the hook-settled types
   * (wanted / protect / destroy / bodyguard / ad / toll) reject any fulfil.
   */
  virtual bool CanFulfil (const JobContext& jc, const Job& job,
                          const Account& executor,
                          const Json::Value& args) const = 0;

  /**
   * Performs the fulfil: moves the type-specific goods and settles the coins
   * and completion counters.  Returns COMPLETE (the generic op then deletes
   * the row) or PROGRESS (the row stays, with the mutated proto persisted --
   * patrol check-ins and partial cargo deliveries).  Must not delete the row.
   */
  virtual FulfilResult DoFulfil (const JobContext& jc, Job& job,
                                 Account& executor,
                                 const Json::Value& args) const = 0;

  /**
   * Settles a job whose deadline has passed (confirmed block only).  Handles
   * both the OPEN case (always void + refund the poster) and the ACCEPTED
   * case (type-specific: the delivery family fails and forfeits collateral to
   * the poster; the success-on-expiry types -- protect, bodyguard, ad-slot,
   * toll -- pay the worker).  Kills are processed before the expiry sweep
   * within a block, so "alive at expiry" is well-defined.  Returns the
   * outcome the caller records in the job_history table before deleting
   * the row.
   */
  virtual JobOutcome OnExpire (const JobContext& jc, Job& job) const = 0;

  /**
   * Settles a job whose linked entity (building or character) was destroyed
   * (confirmed block only).  Returns the outcome the caller records in the
   * job_history table before deleting the row.  Types that never set
   * linked_id keep this default, which must never be reached.
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
   * unaffected by the transfer (the default: transport/haul deliver to the
   * building regardless of owner, protect/destroy wager on its survival).
   * Only the ad-slot type reacts, voiding + refunding the advertiser -- the
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
   * Pays out for one qualifying kill on a linked_name job (the wanted-bounty
   * pool): one tranche split across the distinct killer accounts.  Returns
   * true when the pool is drained and the caller should delete the row.
   * Only the wanted type implements this.
   */
  virtual bool
  OnTargetKill (const JobContext& jc, Job& job,
                const std::set<std::string>& killOwners) const
  {
    LOG (FATAL)
        << "Job " << job.GetId () << " has no target-kill settlement";
  }

};

/**
 * Returns the predicate object for a job type, or nullptr for an unknown type.
 * The returned reference is to a process-lifetime singleton.
 */
const JobPredicate* PredicateForType (Job::Type type);

/** Maps a POST move type name ("transport", ...) to a Job::Type.  */
Job::Type JobTypeFromString (const std::string& name);

/** Returns the move/JSON name for a job type, or nullptr if unknown.  */
const char* JobTypeName (Job::Type type);

/** Returns the JSON name for a history outcome, or nullptr if unknown.  */
const char* JobOutcomeName (JobOutcome outcome);

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
 * rows.  Called once per SUPERBLOCK (a deadline passing on an ordinary block
 * settles at the next superblock sweep), AFTER kill processing (the normative
 * phase order: an entity dying in the boundary superblock is a death, not a
 * survival); on the (vast majority of) sweeps where nothing is due it is an
 * indexed no-op that touches no rows.
 *
 * The sweep is deliberately uncapped (no continuation across blocks):  every
 * due row was paid for (posting fee burned, escrow locked), settlement is a
 * constant amount of work per job walked straight off the (deadline, id)
 * index with no sort, and forked-chain stress runs settle 100
 * deadline-aligned jobs inside one ordinary sweep block with negligible cost
 * -- orders of magnitude below the dense-combat processing ceiling measured
 * for the same block budget.  A deterministic cap would defer settlement of
 * already-due jobs to later blocks, re-opening the very window (mutable
 * inputs after the deadline) that JobIsDue exists to close; it stays absent
 * unless a measured bound some day demands it.
 */
void ExpireJobs (Database& db, const Context& ctx);

/**
 * Handles all jobs linked to an entity (building or character) that is being
 * destroyed, running each type's OnLinkedEntityDestroyed settlement and
 * deleting the rows.  Must be called from the combat kill-processor while the
 * entity row still exists.  Runs only on the rare event of an actual death,
 * and is an indexed no-op when no job is linked to the entity.
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
 * Per-block resolver for the wanted board: pays bounty tranches for
 * qualifying character kills.  Constructed once per block alongside the
 * FameUpdater and fed every kill from the same pre-removal pass (damage lists
 * and victim rows still live), sharing fame's distinct-owner derivation
 * semantics.  The constructor loads the (small, bounded) set of names under
 * an active bounty, so the common per-death path is one hash lookup and SQL
 * is only issued for actual bounty events -- the mega-battle perf guard.
 */
class JobsBountyTracker
{

private:

  Database& db;
  const Context& ctx;

  /** The block's damage lists (owned by the FameUpdater).  */
  const DamageLists& dl;

  /** Character table for owner lookups on still-live rows.  */
  CharacterTable characters;

  /** Names under an active bounty, loaded once per block.  */
  std::set<std::string> bountyNames;

public:

  explicit JobsBountyTracker (Database& d, const Context& c,
                              const DamageLists& l);

  JobsBountyTracker () = delete;
  JobsBountyTracker (const JobsBountyTracker&) = delete;
  void operator= (const JobsBountyTracker&) = delete;

  /**
   * Processes one killed fighter: if it is a character whose owner is under
   * bounty, pays one tranche per pool on that name, split across the distinct
   * accounts on the victim's damage list.  Kills with an empty owner set
   * (e.g. turret-only damage) pay nothing and consume nothing.
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
