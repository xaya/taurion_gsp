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

#include "jobs.hpp"

#include "buildings.hpp"
#include "jsonutils.hpp"

#include "hexagonal/coord.hpp"

#include <xayautil/jsonutils.hpp>

#include <glog/logging.h>

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pxd
{

bool
IsSameOrNeutralDestination (const Building& b, const Faction f)
{
  return b.GetFaction () == f || b.GetFaction () == Faction::ANCIENT;
}

/* ************************************************************************** */
/* Shared settlement helpers.                                                 */

namespace
{

/** Upper bound on a patrol area radius (L1 hex distance).  */
constexpr unsigned MAX_PATROL_RADIUS = 1'000;
/** Upper bound on the number of patrol check-ins.  */
constexpr unsigned MAX_PATROL_CHECKINS = 1'000;
/** Upper bound on the length of an ad content hash (hex string).  */
constexpr size_t MAX_AD_HASH_LENGTH = 200;

/**
 * Fetches an account by name, CHECK-failing if it does not exist.  Callers
 * must ensure no other live handle to the same account row exists
 * (UniqueHandles): safe in the block hooks, but move-op code must use the
 * executor handle it already holds for that account.
 */
AccountsTable::Handle
GetAccountChecked (const JobContext& jc, const std::string& name)
{
  auto a = jc.accounts.GetByName (name);
  CHECK (a != nullptr) << "Job party account missing: " << name;
  return a;
}

/**
 * Bumps the per-account completion counters (the on-chain completion
 * artifact, read by the reputation layer and the approval flow).
 */
void
BumpJobStats (Account& a, const bool completed, const Amount value)
{
  auto& pb = a.MutableProto ();
  if (completed)
    {
      pb.set_jobs_completed (pb.jobs_completed () + 1);
      pb.set_jobs_value_completed (pb.jobs_value_completed () + value);
    }
  else
    pb.set_jobs_failed (pb.jobs_failed () + 1);
}

/**
 * Whether a deadlined job is at or past its deadline (deadline <= now, the
 * exclusive boundary): such a job belongs to the expiry sweep, and the
 * lifecycle operations must not touch it.  The sweep only runs on superblocks
 * while moves run on every block, so without this guard an operation landing
 * between the deadline and the next superblock could change the settlement:
 * a fulfil would turn an expiry FAILED into COMPLETED, and an accept would
 * resurrect a listing that should void (the work-window rewrite pushes the
 * deadline forward; an elapsed ad would pay its full rent for zero display
 * time).  Standing jobs (no deadline) are never due.
 */
bool
JobIsDue (const Job& job, const JobContext& jc)
{
  return job.HasDeadline () && job.GetDeadline () <= jc.ctx.Timestamp ();
}

/**
 * Validates that a job's terms carry zero worker collateral (the open-claim
 * and payer/payee-swapped types, where nobody posts a bond).  The generic
 * parse has already verified "co" is a well-formed amount.
 */
bool
RequireZeroCollateral (const Json::Value& terms, const char* what)
{
  Amount collateral;
  CHECK (CoinAmountFromJson (terms["co"], collateral));
  if (collateral != 0)
    {
      LOG (WARNING) << what << " takes no collateral";
      return false;
    }
  return true;
}

/**
 * Parses a character id out of the given terms field and validates that it
 * exists and is owned by the poster (the protectee / traveller checks).
 */
bool
ValidateOwnedCharacter (const JobContext& jc, const Json::Value& terms,
                        const char* field, const Account& poster,
                        const char* label, Database::IdT& id)
{
  if (!IdFromJson (terms[field], id))
    return false;
  const auto c = jc.characters.GetById (id);
  if (c == nullptr || c->GetOwner () != poster.GetName ())
    {
      LOG (WARNING)
          << label << " " << id << " missing or not owned by "
          << poster.GetName ();
      return false;
    }
  return true;
}

/**
 * Pays a worker for a successful job: their locked collateral plus the reward
 * (fee-free), and bumps their completion counters.  The account handle is
 * passed in -- every caller already holds it (the fulfil executor, or the
 * hook's fetched worker).
 */
void
PayWorkerSuccess (Account& worker, const Job& job)
{
  ReleaseJobCoins (worker, job.GetReward () + job.GetCollateral ());
  BumpJobStats (worker, true, job.GetReward ());
}

/**
 * Hook-path settlement: the job succeeded (success-on-expiry types).  Pays
 * the worker the reward plus their collateral back and bumps their counters.
 * Must not be called while any account handle is live.
 */
void
SettleSuccessAtHook (const JobContext& jc, const Job& job)
{
  auto worker = GetAccountChecked (jc, job.GetWorker ());
  PayWorkerSuccess (*worker, job);
}

/**
 * Hook-path settlement: the accepted worker failed (expiry or linked-entity
 * failure).  The reward refunds and the collateral forfeits, both to the
 * poster (the uniform forfeit-to-poster rule), and the worker's failed
 * counter bumps.  The poster's forfeit-as-poster counter bumps too: they
 * can force this outcome at will (e.g. by suiciding their own protected
 * asset), so the mark lets workers vet collateral-harvesting posters.
 * Must not be called while any account handle is live.
 */
void
SettleFailureAtHook (const JobContext& jc, const Job& job)
{
  {
    auto poster = GetAccountChecked (jc, job.GetPoster ());
    ReleaseJobCoins (*poster, job.GetReward () + job.GetCollateral ());
    auto& pb = poster->MutableProto ();
    pb.set_jobs_failed_as_poster (pb.jobs_failed_as_poster () + 1);
  }
  auto worker = GetAccountChecked (jc, job.GetWorker ());
  BumpJobStats (*worker, false, 0);
}

/**
 * Hook-path settlement: the job is void through neither party's fault (an
 * OPEN job expired, or a linked entity died on a no-fault type).  The reward
 * refunds to the poster and any locked collateral returns to the worker; no
 * counters change.  Must not be called while any account handle is live.
 */
void
VoidJobAtHook (const JobContext& jc, const Job& job)
{
  {
    auto poster = GetAccountChecked (jc, job.GetPoster ());
    ReleaseJobCoins (*poster, job.GetReward ());
  }
  if (job.GetStatus () == Job::Status::ACCEPTED)
    {
      auto worker = GetAccountChecked (jc, job.GetWorker ());
      ReleaseJobCoins (*worker, job.GetCollateral ());
    }
}

/**
 * Records a settled job in the history table, then deletes its live row --
 * releasing the row handle first so the delete cannot collide with it on the
 * unique-handle tracker.  Every terminal transition funnels through here, so
 * "write the history row before deleting the job" is a structural guarantee
 * rather than a per-site convention.
 */
void
SettleAndDelete (JobsTable& jobs, JobsTable::Handle job,
                 const JobOutcome outcome, const Context& ctx)
{
  const auto id = job->GetId ();
  /* Like the DEX trade history, the record stores the REAL chain block
     (settlements happen on every block, e.g. through moves), not the
     superblock counter -- ctx.Height() would stamp a non-superblock
     settlement with a superblock height that has not happened yet.  */
  jobs.WriteHistory (*job, outcome, ctx.BlockHeight (), ctx.Timestamp ());
  job.reset ();
  jobs.DeleteById (id);
}

/**
 * Shared base for every type that settles only through the block hooks
 * (expiry / entity death / target kill) and therefore rejects any fulfil op.
 */
class HookSettledPredicate : public JobPredicate
{

public:

  bool
  CanFulfil (const JobContext& jc, const Job& job, const Account& executor,
             const Json::Value& args) const override
  {
    /* Settles at a hook; a stray fulfil is rejected.  */
    return false;
  }

  FulfilResult
  DoFulfil (const JobContext& jc, Job& job, Account& executor,
            const Json::Value& args) const override
  {
    LOG (FATAL) << "Job " << job.GetId () << " cannot be fulfilled";
  }

};

/* ************************************************************************** */
/* Delivery family (transport + haul).                                        */

/**
 * Shared predicate base for the delivery family: an outstanding manifest of
 * goods that must end up at the destination building (the job's linked_id
 * once fulfilment is possible).  Fulfil comes in two variants:
 *
 *  - "ch": a progress dump -- whatever of the outstanding manifest the named
 *    character carries (docked at the destination, which may be a foundation)
 *    is delivered, and the job completes when the manifest is empty.  This is
 *    how convoys and bit-by-bit foundation supply work; the reward still
 *    settles all-or-nothing at completion.
 *  - without "ch": all-at-once from the worker's own building inventory at
 *    the destination (multi-trip staging; the staged goods remain the
 *    worker's until this moment).  Foundations have no per-account
 *    inventories, so this variant is for completed buildings only.
 *
 * Where the goods land: a foundation's construction inventory (which may
 * kick off the actual construction), else the poster's inventory at the
 * building.  If the foundation completes mid-job, the building keeps its id
 * and the remaining deliveries simply land in the poster's inventory -- the
 * job is "deliver the goods", not "build the building", so the courier is
 * never cheated out of a completed delivery by someone else finishing the
 * build.
 */
class DeliveryPredicate : public JobPredicate
{

private:

  /** Returns the job's outstanding-manifest proto.  */
  virtual const proto::Inventory& Manifest (const Job& job) const = 0;

  /** Returns the mutable outstanding-manifest proto.  */
  virtual proto::Inventory* MutableManifest (Job& job) const = 0;

protected:

  /**
   * Parses and validates the item manifest out of a POST's terms.  Returns
   * false if malformed, empty, over the entry bound, or referencing an
   * unknown item type.
   */
  static bool ParseManifest (const JobContext& jc, const Json::Value& terms,
                             std::map<std::string, Quantity>& out);

  /**
   * Snapshots a manifest proto into a sorted std::map, so consensus logic
   * never iterates the proto map directly.
   */
  static void
  SnapshotManifest (const proto::Inventory& m,
                    std::map<std::string, Quantity>& out)
  {
    for (const auto& entry : m.fungible ())
      out.emplace (entry.first, entry.second);
  }

public:

  bool CanFulfil (const JobContext& jc, const Job& job,
                  const Account& executor,
                  const Json::Value& args) const override;
  FulfilResult DoFulfil (const JobContext& jc, Job& job, Account& executor,
                         const Json::Value& args) const override;
  JobOutcome OnExpire (const JobContext& jc, Job& job) const override;

};

bool
DeliveryPredicate::ParseManifest (const JobContext& jc,
                                  const Json::Value& terms,
                                  std::map<std::string, Quantity>& out)
{
  const auto& items = terms["items"];
  if (!items.isObject () || items.empty ())
    return false;

  const unsigned maxEntries
      = jc.ctx.RoConfig ()->params ().max_manifest_entries ();
  if (items.size () > maxEntries)
    return false;

  for (const auto& key : items.getMemberNames ())
    {
      Quantity q;
      if (!QuantityFromJson (items[key], q) || q <= 0)
        return false;
      if (jc.ctx.RoConfig ().ItemOrNull (key) == nullptr)
        return false;
      out.emplace (key, q);
    }

  return true;
}

bool
DeliveryPredicate::CanFulfil (const JobContext& jc, const Job& job,
                              const Account& executor,
                              const Json::Value& args) const
{
  const auto destB = job.GetLinkedId ();
  std::map<std::string, Quantity> outstanding;
  SnapshotManifest (Manifest (job), outstanding);
  CHECK (!outstanding.empty ()) << "Empty outstanding manifest on job "
                                << job.GetId ();

  if (args.isMember ("ch"))
    {
      /* Progress-dump variant: the character must be docked at the
         destination (foundations included) and carry at least one
         deliverable unit, so a fulfil is never a no-op.  */
      Database::IdT chId;
      if (!IdFromJson (args["ch"], chId))
        return false;
      const auto ch = jc.characters.GetById (chId);
      if (ch == nullptr || ch->GetOwner () != executor.GetName ())
        {
          LOG (WARNING)
              << "Character " << chId << " missing or not owned by "
              << executor.GetName ();
          return false;
        }
      if (!ch->IsInBuilding () || ch->GetBuildingId () != destB)
        {
          LOG (WARNING)
              << "Character " << chId << " is not docked at destination "
              << destB;
          return false;
        }
      const auto& inv = ch->GetInventory ();
      for (const auto& entry : outstanding)
        if (inv.GetFungibleCount (entry.first) > 0)
          return true;
      return false;
    }

  /* Own-inventory (multi-trip) variant: the worker has staged the FULL
     outstanding manifest into their own building inventory at B.  A
     foundation has no per-account inventory, so only the character-cargo
     variant works there.  */
  const auto b = jc.buildings.GetById (destB);
  if (b == nullptr || b->GetProto ().foundation ())
    return false;
  const auto src = jc.buildingInv.Get (destB, executor.GetName ());
  const auto& inv = src->GetInventory ();
  for (const auto& entry : outstanding)
    if (inv.GetFungibleCount (entry.first)
          < static_cast<Quantity> (entry.second))
      return false;
  return true;
}

FulfilResult
DeliveryPredicate::DoFulfil (const JobContext& jc, Job& job, Account& executor,
                             const Json::Value& args) const
{
  const auto destB = job.GetLinkedId ();

  std::map<std::string, Quantity> outstanding;
  SnapshotManifest (Manifest (job), outstanding);

  if (args.isMember ("ch"))
    {
      /* Progress dump: deliver min(carried, outstanding) per item from the
         character's cargo.  */
      Database::IdT chId;
      CHECK (IdFromJson (args["ch"], chId));

      std::map<std::string, Quantity> take;
      {
        auto ch = jc.characters.GetById (chId);
        CHECK (ch != nullptr);
        for (const auto& entry : outstanding)
          {
            const Quantity carried
                = ch->GetInventory ().GetFungibleCount (entry.first);
            const Quantity t = std::min<Quantity> (carried, entry.second);
            if (t > 0)
              {
                ch->GetInventory ().AddFungibleCount (entry.first, -t);
                take.emplace (entry.first, t);
              }
          }
      }
      CHECK (!take.empty ());

      /* Deposit into the destination: a foundation's construction inventory
         (maybe starting the build), else the poster's inventory.  */
      {
        auto b = jc.buildings.GetById (destB);
        CHECK (b != nullptr);
        if (b->GetProto ().foundation ())
          {
            Inventory inv (*b->MutableProto ()
                               .mutable_construction_inventory ());
            for (const auto& entry : take)
              inv.AddFungibleCount (entry.first, entry.second);
            MaybeStartBuildingConstruction (*b, jc.ongoings, jc.ctx);
          }
        else
          {
            auto tgt = jc.buildingInv.Get (destB, job.GetPoster ());
            for (const auto& entry : take)
              tgt->GetInventory ().AddFungibleCount (entry.first,
                                                     entry.second);
          }
      }

      /* Shrink the outstanding manifest by what was delivered.  */
      bool done = true;
      {
        Inventory rest (*MutableManifest (job));
        for (const auto& entry : take)
          rest.AddFungibleCount (entry.first, -entry.second);
        for (const auto& entry : outstanding)
          if (rest.GetFungibleCount (entry.first) > 0)
            done = false;
      }

      if (!done)
        {
          LOG (INFO)
              << executor.GetName () << " partially fulfilled delivery job "
              << job.GetId () << " (character " << chId << ")";
          return FulfilResult::PROGRESS;
        }
    }
  else
    {
      /* All-at-once from the worker's own inventory at B.  CanFulfil has
         verified the destination is not a foundation and fully covered.  */
      {
        auto src = jc.buildingInv.Get (destB, executor.GetName ());
        for (const auto& entry : outstanding)
          src->GetInventory ().AddFungibleCount (entry.first, -entry.second);
      }
      auto tgt = jc.buildingInv.Get (destB, job.GetPoster ());
      tgt->GetInventory () += Inventory (Manifest (job));
    }

  LOG (INFO)
      << executor.GetName () << " fulfilled delivery job " << job.GetId ()
      << " to building " << destB;

  /* The manifest is delivered in full: pay the worker the reward and return
     their collateral (fee-free), and bump their completion counters.  */
  PayWorkerSuccess (executor, job);
  return FulfilResult::COMPLETE;
}

JobOutcome
DeliveryPredicate::OnExpire (const JobContext& jc, Job& job) const
{
  if (job.GetStatus () == Job::Status::ACCEPTED)
    {
      /* The worker failed to deliver in time: the reward refunds and the
         collateral forfeits, both to the poster.  Any partial progress-dumps
         stay delivered (the worker's priced risk); any privately staged goods
         remain the worker's own.  */
      LOG (INFO)
          << "Delivery job " << job.GetId () << " expired unfulfilled; "
          << job.GetReward () << " + forfeited " << job.GetCollateral ()
          << " to poster " << job.GetPoster ();
      SettleFailureAtHook (jc, job);
      return JobOutcome::FAILED;
    }

  LOG (INFO)
      << "Open delivery job " << job.GetId () << " expired; refunding "
      << job.GetReward () << " to poster " << job.GetPoster ();
  VoidJobAtHook (jc, job);
  return JobOutcome::VOID;
}

/* ************************************************************************** */

/**
 * Transport: the worker procures the goods from anywhere and delivers them
 * to the destination building (the linked_id).
 */
class TransportPredicate : public DeliveryPredicate
{

private:

  const proto::Inventory&
  Manifest (const Job& job) const override
  {
    return job.GetProto ().transport ().manifest ();
  }

  proto::Inventory*
  MutableManifest (Job& job) const override
  {
    return job.MutableProto ().mutable_transport ()->mutable_manifest ();
  }

public:

  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::BUILDING;
  }

  bool ValidatePost (const JobContext& jc, const Account& poster,
                     const Json::Value& terms) const override;
  void ApplyPost (const JobContext& jc, Account& poster,
                  const Json::Value& terms, Job& job) const override;
  JobOutcome OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const override;

};

bool
TransportPredicate::ValidatePost (const JobContext& jc, const Account& poster,
                                  const Json::Value& terms) const
{
  std::map<std::string, Quantity> manifest;
  if (!ParseManifest (jc, terms, manifest))
    {
      LOG (WARNING) << "Invalid transport manifest:\n" << terms;
      return false;
    }

  Database::IdT destB;
  if (!IdFromJson (terms["to"], destB))
    return false;

  const auto b = jc.buildings.GetById (destB);
  if (b == nullptr)
    {
      LOG (WARNING) << "Transport destination " << destB << " does not exist";
      return false;
    }
  /* Foundations are allowed (construction-supply) as well as normal
     buildings; only the faction must be reachable.  */
  if (!IsSameOrNeutralDestination (*b, poster.GetFaction ()))
    {
      LOG (WARNING)
          << "Transport destination " << destB
          << " is not same-faction-or-neutral for " << poster.GetName ();
      return false;
    }

  return true;
}

void
TransportPredicate::ApplyPost (const JobContext& jc, Account& poster,
                               const Json::Value& terms, Job& job) const
{
  Database::IdT destB;
  CHECK (IdFromJson (terms["to"], destB));
  job.SetLinkedId (destB);

  std::map<std::string, Quantity> manifest;
  CHECK (ParseManifest (jc, terms, manifest));
  auto& fungible
      = *job.MutableProto ().mutable_transport ()->mutable_manifest ()
             ->mutable_fungible ();
  for (const auto& entry : manifest)
    fungible[entry.first] = entry.second;
}

JobOutcome
TransportPredicate::OnLinkedEntityDestroyed (const JobContext& jc,
                                             Job& job) const
{
  /* The destination building died -- not the worker's fault.  Refund the
     reward to the poster and any locked collateral back to the worker.  */
  LOG (INFO)
      << "Voided transport job " << job.GetId ()
      << ": destination building " << job.GetLinkedId () << " destroyed";
  VoidJobAtHook (jc, job);
  return JobOutcome::VOID;
}

/* ************************************************************************** */

/**
 * Haul: like transport, but the POSTER supplies the goods.  They are taken
 * out of the poster's inventory at the source building A at post time (held
 * by the job), handed to the worker's inventory at A on accept (when the
 * linked entity swaps from A to the destination B), and then delivered like
 * any other delivery job.
 */
class HaulPredicate : public DeliveryPredicate
{

private:

  const proto::Inventory&
  Manifest (const Job& job) const override
  {
    return job.GetProto ().haul ().manifest ();
  }

  proto::Inventory*
  MutableManifest (Job& job) const override
  {
    return job.MutableProto ().mutable_haul ()->mutable_manifest ();
  }

  /** Returns the reserved goods to the poster's inventory at the source.  */
  void
  ReturnGoodsToPoster (const JobContext& jc, const Job& job) const
  {
    const auto& hp = job.GetProto ().haul ();
    auto inv = jc.buildingInv.Get (hp.source_building (), job.GetPoster ());
    inv->GetInventory () += Inventory (hp.manifest ());
  }

public:

  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::BUILDING;
  }

  /**
   * Re-verifies the destination still exists at accept time.  While OPEN the
   * linked entity watches the SOURCE, so a destination destroyed in between
   * would otherwise slip through and leave the accepted job pointing at a
   * dead building the worker can never deliver to.
   */
  bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const override
  {
    const auto dest = job.GetProto ().haul ().dest_building ();
    if (jc.buildings.GetById (dest) == nullptr)
      {
        LOG (WARNING)
            << "Haul destination " << dest << " of job " << job.GetId ()
            << " no longer exists";
        return false;
      }
    return true;
  }

  bool ValidatePost (const JobContext& jc, const Account& poster,
                     const Json::Value& terms) const override;
  void ApplyPost (const JobContext& jc, Account& poster,
                  const Json::Value& terms, Job& job) const override;
  void OnAccept (const JobContext& jc, Job& job,
                 Account& worker) const override;
  void OnCancel (const JobContext& jc, Job& job) const override;
  JobOutcome OnExpire (const JobContext& jc, Job& job) const override;
  JobOutcome OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const override;

};

bool
HaulPredicate::ValidatePost (const JobContext& jc, const Account& poster,
                             const Json::Value& terms) const
{
  std::map<std::string, Quantity> manifest;
  if (!ParseManifest (jc, terms, manifest))
    {
      LOG (WARNING) << "Invalid haul manifest:\n" << terms;
      return false;
    }

  Database::IdT srcA, destB;
  if (!IdFromJson (terms["from"], srcA) || !IdFromJson (terms["to"], destB)
        || srcA == destB)
    return false;

  {
    const auto a = jc.buildings.GetById (srcA);
    /* The source must be a completed building: the goods sit in per-account
       inventories there, which foundations do not have.  */
    if (a == nullptr || a->GetProto ().foundation ()
          || !IsSameOrNeutralDestination (*a, poster.GetFaction ()))
      {
        LOG (WARNING) << "Invalid haul source " << srcA;
        return false;
      }
  }
  {
    const auto b = jc.buildings.GetById (destB);
    if (b == nullptr || !IsSameOrNeutralDestination (*b, poster.GetFaction ()))
      {
        LOG (WARNING) << "Invalid haul destination " << destB;
        return false;
      }
  }

  /* The poster must actually hold the goods at the source.  */
  const auto inv = jc.buildingInv.Get (srcA, poster.GetName ());
  for (const auto& entry : manifest)
    if (inv->GetInventory ().GetFungibleCount (entry.first)
          < static_cast<Quantity> (entry.second))
      {
        LOG (WARNING)
            << poster.GetName () << " does not hold " << entry.second
            << " of " << entry.first << " at building " << srcA;
        return false;
      }

  return true;
}

void
HaulPredicate::ApplyPost (const JobContext& jc, Account& poster,
                          const Json::Value& terms, Job& job) const
{
  Database::IdT srcA, destB;
  CHECK (IdFromJson (terms["from"], srcA));
  CHECK (IdFromJson (terms["to"], destB));

  std::map<std::string, Quantity> manifest;
  CHECK (ParseManifest (jc, terms, manifest));

  /* Reserve the goods: they leave the poster's inventory and are held by the
     job (recorded in the manifest) until accept or unwind.  */
  {
    auto inv = jc.buildingInv.Get (srcA, poster.GetName ());
    for (const auto& entry : manifest)
      inv->GetInventory ().AddFungibleCount (entry.first, -entry.second);
  }

  auto* hp = job.MutableProto ().mutable_haul ();
  hp->set_source_building (srcA);
  hp->set_dest_building (destB);
  auto& fungible = *hp->mutable_manifest ()->mutable_fungible ();
  for (const auto& entry : manifest)
    fungible[entry.first] = entry.second;

  /* While OPEN, the job's fate is tied to the source building (the reserved
     goods are notionally there); accept swaps the link to the destination.  */
  job.SetLinkedId (srcA);
}

void
HaulPredicate::OnAccept (const JobContext& jc, Job& job, Account& worker) const
{
  const auto& hp = job.GetProto ().haul ();

  /* Hand the reserved goods to the worker's inventory at the source, ready
     to be loaded and hauled.  From here on they are in the worker's custody
     (the collateral is the poster's cover).  */
  {
    auto inv = jc.buildingInv.Get (hp.source_building (), worker.GetName ());
    inv->GetInventory () += Inventory (hp.manifest ());
  }

  job.SetLinkedId (hp.dest_building ());
}

void
HaulPredicate::OnCancel (const JobContext& jc, Job& job) const
{
  /* Cancelling an OPEN haul returns the reserved goods along with the
     generic reward refund.  */
  ReturnGoodsToPoster (jc, job);
}

JobOutcome
HaulPredicate::OnExpire (const JobContext& jc, Job& job) const
{
  /* An OPEN haul still holds the poster's reserved goods -- return them
     before the generic void.  The ACCEPTED unwind is the shared delivery
     one (the worker has the goods; the collateral compensates).  */
  if (job.GetStatus () == Job::Status::OPEN)
    ReturnGoodsToPoster (jc, job);
  return DeliveryPredicate::OnExpire (jc, job);
}

JobOutcome
HaulPredicate::OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const
{
  if (job.GetStatus () == Job::Status::OPEN)
    {
      /* The SOURCE building died with the reserved goods notionally inside:
         the job voids, the reward refunds, and the goods drop as ground loot
         at the source (its inventories are about to be dropped likewise).  */
      const auto& hp = job.GetProto ().haul ();
      {
        const auto b = jc.buildings.GetById (hp.source_building ());
        CHECK (b != nullptr);
        auto ground = jc.groundLoot.GetByCoord (b->GetCentre ());
        ground->GetInventory () += Inventory (hp.manifest ());
      }
      LOG (INFO)
          << "Voided open haul job " << job.GetId () << ": source building "
          << hp.source_building () << " destroyed, goods dropped as loot";
      VoidJobAtHook (jc, job);
      return JobOutcome::VOID;
    }

  /* ACCEPTED: the destination died mid-haul.  Not the worker's fault, but the
     worker still holds the poster's goods (handed over at accept, possibly
     already loaded and in transit, so unrecoverable here).  The collateral is
     the poster's cover for exactly this loss: forfeit it to the poster to
     compensate for the goods, alongside the reward refund.  Neither side is
     marked at fault -- the destruction was a third party's doing.  */
  {
    auto poster = GetAccountChecked (jc, job.GetPoster ());
    ReleaseJobCoins (*poster, job.GetReward () + job.GetCollateral ());
  }
  LOG (INFO)
      << "Voided haul job " << job.GetId () << ": destination building "
      << job.GetLinkedId () << " destroyed; collateral compensates the poster";
  return JobOutcome::VOID;
}

/* ************************************************************************** */
/* Wanted board.                                                              */

/**
 * Wanted-bounty: a standing, open-claim pool on an account name (the
 * linked_name).  Each qualifying kill of one of the target's characters pays
 * one tranche, split equally across the distinct accounts on the victim's
 * damage list; the pool completes when drained.  No accept step, no
 * collateral, no fulfil op -- settlement happens entirely at the kill hook,
 * and the only exit is the notice-based cancel.
 */
class WantedPredicate : public HookSettledPredicate
{

public:

  bool
  IsStanding () const override
  {
    return true;
  }

  bool
  SettlesOnTargetKill () const override
  {
    return true;
  }

  Faction
  AudienceFaction (const Account& poster) const override
  {
    /* Visible to everyone; mechanically only the target's enemies can land
       qualifying damage anyway (no friendly fire).  */
    return Faction::INVALID;
  }

  bool ValidatePost (const JobContext& jc, const Account& poster,
                     const Json::Value& terms) const override;
  void ApplyPost (const JobContext& jc, Account& poster,
                  const Json::Value& terms, Job& job) const override;

  bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const override
  {
    /* Open-claim: there is no accept step.  */
    return false;
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    /* Only reachable when a notice-cancel converted the standing pool into
       an expiring one and the notice ran out: refund whatever is unearned.  */
    CHECK (job.GetStatus () == Job::Status::OPEN);
    LOG (INFO)
        << "Wanted board " << job.GetId () << " closed; refunding unearned "
        << job.GetReward () << " to " << job.GetPoster ();
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

  bool OnTargetKill (const JobContext& jc, Job& job,
                     const std::set<std::string>& killOwners) const override;

};

bool
WantedPredicate::ValidatePost (const JobContext& jc, const Account& poster,
                               const Json::Value& terms) const
{
  if (!terms["name"].isString ())
    return false;
  const std::string target = terms["name"].asString ();
  if (target == poster.GetName ())
    {
      LOG (WARNING) << poster.GetName () << " cannot post a bounty on itself";
      return false;
    }
  {
    const auto t = jc.accounts.GetByName (target);
    if (t == nullptr || !t->IsInitialised ())
      {
        LOG (WARNING) << "Bounty target does not exist: " << target;
        return false;
      }
  }

  if (!terms["n"].isUInt () || !xaya::IsIntegerValue (terms["n"]))
    return false;
  const unsigned quota = terms["n"].asUInt ();
  const unsigned maxQuota = jc.ctx.RoConfig ()->params ().max_bounty_quota ();
  if (quota < 1 || quota > maxQuota)
    {
      LOG (WARNING) << "Bounty quota out of range: " << quota;
      return false;
    }

  if (!RequireZeroCollateral (terms, "A wanted board"))
    return false;

  Amount reward;
  CHECK (CoinAmountFromJson (terms["r"], reward));
  if (reward / quota < 1)
    {
      LOG (WARNING)
          << "Bounty reward " << reward << " is too small for quota " << quota;
      return false;
    }

  return true;
}

void
WantedPredicate::ApplyPost (const JobContext& jc, Account& poster,
                            const Json::Value& terms, Job& job) const
{
  job.SetLinkedName (terms["name"].asString ());

  Amount reward;
  CHECK (CoinAmountFromJson (terms["r"], reward));
  const unsigned quota = terms["n"].asUInt ();

  auto* wp = job.MutableProto ().mutable_wanted ();
  wp->set_quota (quota);
  wp->set_remaining (quota);
  wp->set_tranche (reward / quota);
}

bool
WantedPredicate::OnTargetKill (const JobContext& jc, Job& job,
                               const std::set<std::string>& killOwners) const
{
  CHECK (!killOwners.empty ());
  auto* wp = job.MutableProto ().mutable_wanted ();
  CHECK_GT (wp->remaining (), 0);

  const Amount tranche = wp->tranche ();
  CHECK_GE (job.GetReward (), tranche);

  /* One tranche leaves the escrow: split equally across the distinct killer
     accounts, with any division remainder burned (never redistributed).  */
  const Amount share = tranche / killOwners.size ();
  /* When the tranche cannot give every distinct killer at least one coin,
     nobody is paid -- crediting a zero-value completion would inflate the
     reputation counters for free.  The tranche is still consumed (burned as
     the remainder, which is never redistributed).  */
  if (share > 0)
    for (const auto& owner : killOwners)
      {
        auto a = GetAccountChecked (jc, owner);
        ReleaseJobCoins (*a, share);
        BumpJobStats (*a, true, share);
      }

  job.SetReward (job.GetReward () - tranche);
  wp->set_remaining (wp->remaining () - 1);

  LOG (INFO)
      << "Wanted board " << job.GetId () << " paid a tranche of " << tranche
      << " split across " << killOwners.size () << " hunter(s); "
      << wp->remaining () << " kill(s) remaining";

  if (wp->remaining () > 0)
    return false;

  /* Pool drained: whatever escrow is left is the division dust, burned by
     never crediting it anywhere.  */
  if (job.GetReward () > 0)
    LOG (INFO)
        << "Wanted board " << job.GetId () << " complete; burning dust "
        << job.GetReward ();
  return true;
}

/* ************************************************************************** */
/* Entity-fate family (protect / destroy / bodyguard).                        */

/**
 * Shared base for the entity-fate family: an approval-required, exclusive
 * job whose settlement is decided entirely by whether the linked entity
 * (building or character) is destroyed before the deadline.  The two
 * concrete outcomes mirror each other: protect/bodyguard succeed on
 * survival, destroy succeeds on destruction.
 */
class EntityFatePredicate : public HookSettledPredicate
{

private:

  /** Whether the linked entity's DESTRUCTION is the success case.  */
  virtual bool DestructionIsSuccess () const = 0;

  /**
   * The one shared settlement: an OPEN job always voids; an ACCEPTED one
   * succeeds or fails purely by whether the entity's fate matches the type's
   * success case.  Kills run before the expiry sweep, so "alive at expiry"
   * is well-defined.
   */
  JobOutcome
  Settle (const JobContext& jc, Job& job, const bool destroyed) const
  {
    if (job.GetStatus () != Job::Status::ACCEPTED)
      {
        VoidJobAtHook (jc, job);
        return JobOutcome::VOID;
      }

    const bool success = (destroyed == DestructionIsSuccess ());
    LOG (INFO)
        << "Job " << job.GetId () << ": entity " << job.GetLinkedId ()
        << (destroyed ? " destroyed" : " alive at the deadline")
        << ", worker " << job.GetWorker ()
        << (success ? " succeeded" : " failed");
    if (success)
      {
        SettleSuccessAtHook (jc, job);
        return JobOutcome::COMPLETED;
      }
    SettleFailureAtHook (jc, job);
    return JobOutcome::FAILED;
  }

public:

  bool
  RequiresApproval () const override
  {
    return true;
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    return Settle (jc, job, false);
  }

  JobOutcome
  OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const override
  {
    return Settle (jc, job, true);
  }

};

/**
 * Validates a building-target post term ("b"): the building must exist and
 * not be ancient (the starter stations are indestructible, so both fates
 * would be a foregone conclusion).
 */
bool
ValidateBuildingTarget (const JobContext& jc, const Json::Value& terms,
                        Database::IdT& id)
{
  if (!IdFromJson (terms["b"], id))
    return false;
  const auto b = jc.buildings.GetById (id);
  if (b == nullptr || b->GetFaction () == Faction::ANCIENT)
    {
      LOG (WARNING) << "Invalid job target building: " << id;
      return false;
    }
  return true;
}

/** Protect-building: succeed if the building survives to the deadline.  */
class ProtectPredicate : public EntityFatePredicate
{

private:

  bool
  DestructionIsSuccess () const override
  {
    return false;
  }

public:

  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::BUILDING;
  }

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    Database::IdT id;
    return ValidateBuildingTarget (jc, terms, id);
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    Database::IdT id;
    CHECK (IdFromJson (terms["b"], id));
    job.SetLinkedId (id);
    job.MutableProto ().mutable_protect ();
  }

};

/** Destroy-building: succeed if the building dies (by anyone's hand).  */
class DestroyPredicate : public EntityFatePredicate
{

private:

  bool
  DestructionIsSuccess () const override
  {
    return true;
  }

public:

  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::BUILDING;
  }

  Faction
  AudienceFaction (const Account& poster) const override
  {
    /* Open to all factions; the approval flow carries the vetting.  */
    return Faction::INVALID;
  }

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    Database::IdT id;
    return ValidateBuildingTarget (jc, terms, id);
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    Database::IdT id;
    CHECK (IdFromJson (terms["b"], id));
    job.SetLinkedId (id);
    job.MutableProto ().mutable_destroy ();
  }

};

/**
 * Bodyguard: keep the poster's character alive until the deadline.  The
 * protect predicate with a character as the linked entity.
 */
class BodyguardPredicate : public EntityFatePredicate
{

private:

  bool
  DestructionIsSuccess () const override
  {
    return false;
  }

public:

  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::CHARACTER;
  }

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    Database::IdT id;
    return ValidateOwnedCharacter (jc, terms, "ch", poster,
                                   "Bodyguard target", id);
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    Database::IdT id;
    CHECK (IdFromJson (terms["ch"], id));
    job.SetLinkedId (id);
    job.MutableProto ().mutable_bodyguard ();
  }

};

/* ************************************************************************** */
/* Escort.                                                                    */

/**
 * Escort: the worker must get the poster's character to the destination
 * building alive.  The destination building is linked, so its destruction
 * voids and refunds the job.  A dead protectee instead makes the job
 * unfulfillable, and it expires as a failure.
 */
class EscortPredicate : public JobPredicate
{

public:

  bool
  RequiresApproval () const override
  {
    return true;
  }

  /* The destination building is the linked entity: its destruction voids the
     job (the worker can no longer deliver the protectee there through no
     fault of their own).  A dead PROTECTEE, by contrast, is the worker's
     responsibility and stays an expiry failure.  */
  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::BUILDING;
  }

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    Database::IdT chId;
    if (!ValidateOwnedCharacter (jc, terms, "ch", poster,
                                 "Escort protectee", chId))
      return false;

    Database::IdT destB;
    if (!IdFromJson (terms["to"], destB))
      return false;
    const auto b = jc.buildings.GetById (destB);
    if (b == nullptr || !IsSameOrNeutralDestination (*b, poster.GetFaction ()))
      {
        LOG (WARNING) << "Invalid escort destination " << destB;
        return false;
      }

    return true;
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    Database::IdT chId, destB;
    CHECK (IdFromJson (terms["ch"], chId));
    CHECK (IdFromJson (terms["to"], destB));
    auto* ep = job.MutableProto ().mutable_escort ();
    ep->set_protected_character (chId);
    ep->set_dest_building (destB);
    /* Link the destination so its destruction reaches OnLinkedEntityDestroyed
       (the dest_building proto field stays for the fulfil-time dock check).  */
    job.SetLinkedId (destB);
  }

  bool
  CanFulfil (const JobContext& jc, const Job& job, const Account& executor,
             const Json::Value& args) const override
  {
    if (args.size () != 1)
      return false;

    /* The single check: the protected character is alive and docked at the
       destination right now.  */
    const auto& ep = job.GetProto ().escort ();
    const auto c = jc.characters.GetById (ep.protected_character ());
    if (c == nullptr)
      return false;
    return c->IsInBuilding ()
        && c->GetBuildingId () == ep.dest_building ();
  }

  FulfilResult
  DoFulfil (const JobContext& jc, Job& job, Account& executor,
            const Json::Value& args) const override
  {
    LOG (INFO)
        << executor.GetName () << " completed escort job " << job.GetId ();
    PayWorkerSuccess (executor, job);
    return FulfilResult::COMPLETE;
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    if (job.GetStatus () == Job::Status::ACCEPTED)
      {
        LOG (INFO)
            << "Escort job " << job.GetId () << " expired unfulfilled";
        SettleFailureAtHook (jc, job);
        return JobOutcome::FAILED;
      }
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

  JobOutcome
  OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const override
  {
    /* The destination died mid-escort: the worker can no longer deliver the
       protectee there through no fault of their own.  Void -- reward back to
       the poster, collateral back to the worker.  */
    LOG (INFO)
        << "Voided escort job " << job.GetId () << ": destination building "
        << job.GetLinkedId () << " destroyed";
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

};

/* ************************************************************************** */
/* Patrol.                                                                    */

/**
 * Patrol: the worker checks in K times from inside the area, each at least
 * min_spacing seconds after the previous one.  Each check-in is a Progress
 * fulfil; the K-th settles.  The one type with on-chain progress state.
 */
class PatrolPredicate : public JobPredicate
{

public:

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    /* The centre must fit HexCoord's coordinate type, or it would be
       silently narrowed when the check-in validation builds the HexCoord.  */
    using CoordLimits = std::numeric_limits<HexCoord::IntT>;
    for (const auto* axis : {"x", "y"})
      {
        if (!terms[axis].isInt () || !xaya::IsIntegerValue (terms[axis]))
          return false;
        const auto val = terms[axis].asInt ();
        if (val < CoordLimits::min () || val > CoordLimits::max ())
          {
            LOG (WARNING) << "Patrol centre out of range: " << val;
            return false;
          }
      }

    if (!terms["rad"].isUInt () || !xaya::IsIntegerValue (terms["rad"])
          || !terms["k"].isUInt () || !xaya::IsIntegerValue (terms["k"])
          || !terms["sp"].isUInt () || !xaya::IsIntegerValue (terms["sp"]))
      return false;
    const unsigned radius = terms["rad"].asUInt ();
    const unsigned k = terms["k"].asUInt ();
    const unsigned spacing = terms["sp"].asUInt ();
    if (radius < 1 || radius > MAX_PATROL_RADIUS)
      return false;
    if (k < 1 || k > MAX_PATROL_CHECKINS || spacing < 1)
      return false;

    /* The required check-ins must be schedulable within the work window
       (the schedule runs from accept, when the deadline is rewritten), so
       an impossible-by-construction patrol cannot be posted.  The generic
       post validation has already bounds-checked "wd".  */
    const int64_t needed = static_cast<int64_t> (k - 1) * spacing;
    if (!terms["wd"].isInt64 () || needed >= terms["wd"].asInt64 ())
      {
        LOG (WARNING)
            << "Patrol schedule does not fit its work window: " << needed;
        return false;
      }

    return true;
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    auto* pp = job.MutableProto ().mutable_patrol ();
    pp->set_centre_x (terms["x"].asInt ());
    pp->set_centre_y (terms["y"].asInt ());
    pp->set_radius (terms["rad"].asUInt ());
    pp->set_checkins_required (terms["k"].asUInt ());
    pp->set_min_spacing (terms["sp"].asUInt ());
    pp->set_checkins_done (0);
    pp->set_last_checkin (0);
  }

  bool
  CanFulfil (const JobContext& jc, const Job& job, const Account& executor,
             const Json::Value& args) const override
  {
    if (!args.isMember ("ch"))
      return false;
    Database::IdT chId;
    if (!IdFromJson (args["ch"], chId))
      return false;

    const auto c = jc.characters.GetById (chId);
    if (c == nullptr || c->GetOwner () != executor.GetName ())
      return false;
    /* A docked character has no position and is not patrolling.  */
    if (c->IsInBuilding ())
      return false;

    const auto& pp = job.GetProto ().patrol ();
    const HexCoord centre(pp.centre_x (), pp.centre_y ());
    const unsigned dist = HexCoord::DistanceL1 (c->GetPosition (), centre);
    if (dist > pp.radius ())
      {
        LOG (WARNING)
            << "Character " << chId << " is outside the patrol area of job "
            << job.GetId ();
        return false;
      }

    /* Enforce the minimum spacing since the last check-in.  */
    if (pp.checkins_done () > 0
          && jc.ctx.Timestamp () - pp.last_checkin () < pp.min_spacing ())
      {
        LOG (WARNING)
            << "Check-in for job " << job.GetId () << " is too soon";
        return false;
      }

    return true;
  }

  FulfilResult
  DoFulfil (const JobContext& jc, Job& job, Account& executor,
            const Json::Value& args) const override
  {
    auto* pp = job.MutableProto ().mutable_patrol ();
    pp->set_checkins_done (pp->checkins_done () + 1);
    pp->set_last_checkin (jc.ctx.Timestamp ());

    LOG (INFO)
        << executor.GetName () << " checked in for patrol job "
        << job.GetId () << " (" << pp->checkins_done () << " of "
        << pp->checkins_required () << ")";

    if (pp->checkins_done () < pp->checkins_required ())
      return FulfilResult::PROGRESS;

    PayWorkerSuccess (executor, job);
    return FulfilResult::COMPLETE;
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    if (job.GetStatus () == Job::Status::ACCEPTED)
      {
        /* Missed check-ins: all-or-nothing, the whole patrol forfeits.  */
        LOG (INFO)
            << "Patrol job " << job.GetId () << " expired after "
            << job.GetProto ().patrol ().checkins_done () << " check-ins";
        SettleFailureAtHook (jc, job);
        return JobOutcome::FAILED;
      }
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

};

/* ************************************************************************** */
/* Rental.                                                                    */

/**
 * Rental: a fungible item lent by count and returned by count.  The poster
 * is the RENTER (payer): the escrowed reward is rent + deposit in one
 * amount, and the designated worker is the lessor (payee).  Accept moves the
 * items from the lessor to the renter; the renter's fulfil (or the same
 * check at expiry) moves them back and splits the escrow.  A non-return --
 * lost, consumed, destroyed or just late -- defaults the whole escrow to the
 * lessor, so a deposit sized at replacement value turns any loss into a
 * priced involuntary sale.
 */
class RentalPredicate : public JobPredicate
{

private:

  /**
   * Whether the renter currently holds the full rented count in their own
   * inventory at the handover building.
   */
  static bool
  GoodsReturned (const JobContext& jc, const Job& job)
  {
    const auto& rp = job.GetProto ().rental ();
    const auto inv = jc.buildingInv.Get (rp.building (), job.GetPoster ());
    return inv->GetInventory ().GetFungibleCount (rp.item ())
        >= static_cast<Quantity> (rp.count ());
  }

  /**
   * Moves the rented count between two accounts' inventories at the
   * handover building.
   */
  static void
  MoveGoods (const JobContext& jc, const Job& job, const std::string& from,
             const std::string& to)
  {
    const auto& rp = job.GetProto ().rental ();
    {
      auto inv = jc.buildingInv.Get (rp.building (), from);
      inv->GetInventory ().AddFungibleCount (rp.item (), -rp.count ());
    }
    auto inv = jc.buildingInv.Get (rp.building (), to);
    inv->GetInventory ().AddFungibleCount (rp.item (), rp.count ());
  }

  /**
   * Splits the escrow for a clean return: rent to the lessor, deposit back
   * to the renter, completion counters for both sides.  The lessor earned
   * the rent, so only their value counter grows; the renter's completed
   * count records their return reliability without inflating their value by
   * coins they merely paid.  The renter handle is passed in (the fulfil path
   * already holds it); the lessor is fetched.
   */
  static void
  SettleCleanReturn (const JobContext& jc, const Job& job, Account& renter)
  {
    const Amount rent = job.GetProto ().rental ().rent ();
    {
      auto lessor = GetAccountChecked (jc, job.GetWorker ());
      ReleaseJobCoins (*lessor, rent);
      BumpJobStats (*lessor, true, rent);
    }
    ReleaseJobCoins (renter, job.GetReward () - rent);
    BumpJobStats (renter, true, 0);
  }

public:

  bool
  RequiresApproval () const override
  {
    return true;
  }

  bool
  PosterFulfils () const override
  {
    return true;
  }

  /* Rentals are deliberately NOT linked to the handover building: the renter
     is expected to take the item away and use it (swap the hull, fit the
     weapon), so the building's destruction proves nothing about the item's
     fate.  Any loss is simply a non-return at the deadline, covered by the
     deposit -- war risk sits with the renter by design.  */

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    if (!terms["i"].isString ())
      return false;
    const std::string item = terms["i"].asString ();
    if (jc.ctx.RoConfig ().ItemOrNull (item) == nullptr)
      return false;

    Quantity count;
    if (!QuantityFromJson (terms["n"], count) || count <= 0)
      return false;

    if (!RequireZeroCollateral (terms, "A rental"))
      return false;
    Amount reward, rent;
    if (!CoinAmountFromJson (terms["rent"], rent))
      return false;
    if (rent <= 0)
      {
        /* A free rental would let two colluding accounts farm the
           jobs_completed counters on both sides for just the posting fee;
           charging real rent makes every completion move real value.  */
        LOG (WARNING) << "A rental must charge positive rent";
        return false;
      }
    CHECK (CoinAmountFromJson (terms["r"], reward));
    if (rent > reward)
      {
        LOG (WARNING)
            << "Rental rent " << rent << " exceeds the escrow " << reward;
        return false;
      }

    Database::IdT bId;
    if (!IdFromJson (terms["b"], bId))
      return false;
    {
      const auto b = jc.buildings.GetById (bId);
      /* Handover works through per-account building inventories, which
         foundations do not have.  */
      if (b == nullptr || b->GetProto ().foundation ()
            || !IsSameOrNeutralDestination (*b, poster.GetFaction ()))
        {
          LOG (WARNING) << "Invalid rental building " << bId;
          return false;
        }
    }

    if (!terms["w"].isString ())
      return false;
    const std::string lessor = terms["w"].asString ();
    if (lessor == poster.GetName ())
      return false;
    const auto w = jc.accounts.GetByName (lessor);
    if (w == nullptr || !w->IsInitialised ()
          || w->GetFaction () != poster.GetFaction ())
      {
        LOG (WARNING) << "Invalid rental lessor: " << lessor;
        return false;
      }

    return true;
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    Quantity count;
    CHECK (QuantityFromJson (terms["n"], count));
    Amount rent;
    CHECK (CoinAmountFromJson (terms["rent"], rent));

    auto* rp = job.MutableProto ().mutable_rental ();
    rp->set_item (terms["i"].asString ());
    rp->set_count (count);
    rp->set_rent (rent);
    Database::IdT bId;
    CHECK (IdFromJson (terms["b"], bId));
    rp->set_building (bId);

    /* The lessor is the fixed payee: designate them at post time, so the
       approval gate only ever lets them accept.  */
    job.MutableProto ().set_designated_worker (terms["w"].asString ());
  }

  bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const override
  {
    /* The lessor must actually hold the goods at the handover building.  */
    const auto& rp = job.GetProto ().rental ();
    const auto inv = jc.buildingInv.Get (rp.building (), worker.GetName ());
    if (inv->GetInventory ().GetFungibleCount (rp.item ())
          < static_cast<Quantity> (rp.count ()))
      {
        LOG (WARNING)
            << worker.GetName () << " does not hold " << rp.count ()
            << " of " << rp.item () << " to rent out";
        return false;
      }
    return true;
  }

  void
  OnAccept (const JobContext& jc, Job& job, Account& worker) const override
  {
    /* Handover: the rented items move from the lessor to the renter,
       atomically with the accept.  */
    MoveGoods (jc, job, worker.GetName (), job.GetPoster ());
    LOG (INFO)
        << worker.GetName () << " handed over rental job " << job.GetId ();
  }

  bool
  CanFulfil (const JobContext& jc, const Job& job, const Account& executor,
             const Json::Value& args) const override
  {
    if (args.size () != 1)
      return false;
    return GoodsReturned (jc, job);
  }

  FulfilResult
  DoFulfil (const JobContext& jc, Job& job, Account& executor,
            const Json::Value& args) const override
  {
    /* The executor is the POSTER (renter) returning the goods.  */
    MoveGoods (jc, job, executor.GetName (), job.GetWorker ());
    SettleCleanReturn (jc, job, executor);
    LOG (INFO)
        << executor.GetName () << " returned rental job " << job.GetId ();
    return FulfilResult::COMPLETE;
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    if (job.GetStatus () != Job::Status::ACCEPTED)
      {
        VoidJobAtHook (jc, job);
        return JobOutcome::VOID;
      }

    if (GoodsReturned (jc, job))
      {
        /* The renter has the count sitting at the building but never sent
           the fulfil: settle exactly as a clean return.  */
        LOG (INFO)
            << "Rental job " << job.GetId ()
            << " expired with the goods returned; settling cleanly";
        MoveGoods (jc, job, job.GetPoster (), job.GetWorker ());
        auto renter = GetAccountChecked (jc, job.GetPoster ());
        SettleCleanReturn (jc, job, *renter);
        return JobOutcome::COMPLETED;
      }

    /* Non-return: rent + deposit both default to the lessor.  */
    LOG (INFO)
        << "Rental job " << job.GetId () << " expired without return; "
        << job.GetReward () << " defaults to " << job.GetWorker ();
    {
      auto lessor = GetAccountChecked (jc, job.GetWorker ());
      ReleaseJobCoins (*lessor, job.GetReward ());
    }
    auto renter = GetAccountChecked (jc, job.GetPoster ());
    BumpJobStats (*renter, false, 0);
    return JobOutcome::FAILED;
  }

};

/* ************************************************************************** */
/* Ad-slot.                                                                   */

/**
 * Ad-slot rental: the poster (advertiser) escrows the rent on a building's
 * ad slot; the designated worker is the building's owner, whose accept IS
 * the content approval (committed by hash).  The rent pays out at the
 * deadline; the entity hook refunds it if the building dies first.  After
 * accept, rendering is unconditional -- the owner has no take-down lever for
 * the paid period, and clients render only hash-matching content.
 */
class AdPredicate : public HookSettledPredicate
{

public:

  bool
  RequiresApproval () const override
  {
    return true;
  }

  /* An ad rents a CALENDAR window ([post+start, post+d]), not an amount of
     work: accepting must not move the window, so ads take no "wd" and keep
     their posted deadline.  */
  bool
  UsesWorkWindow () const override
  {
    return false;
  }

  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::BUILDING;
  }

  Faction
  AudienceFaction (const Account& poster) const override
  {
    return Faction::INVALID;
  }

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    if (!RequireZeroCollateral (terms, "An ad-slot rental"))
      return false;

    if (!terms["slot"].isUInt () || !xaya::IsIntegerValue (terms["slot"]))
      return false;
    if (!terms["hash"].isString ())
      return false;
    const std::string hash = terms["hash"].asString ();
    if (hash.empty () || hash.size () > MAX_AD_HASH_LENGTH)
      return false;

    /* Optional scheduling: the window opens `start` seconds after post
       (0 / absent = immediately) and closes at the deadline.  The rented
       window itself must satisfy the work-window floor, which also forces
       it to be non-empty and subsumes the listing floor ads are exempt
       from; the deadline cap already bounds how far ahead a slot can be
       booked.  */
    int64_t startSecs = 0;
    if (!terms["start"].isNull ())
      {
        if (!terms["start"].isInt64 () || !xaya::IsIntegerValue (terms["start"]))
          return false;
        startSecs = terms["start"].asInt64 ();
        if (startSecs < 0)
          return false;
      }
    if (!terms["d"].isInt64 ()
          || terms["d"].asInt64 () - startSecs
              < jc.ctx.RoConfig ()->params ().min_work_window ())
      {
        LOG (WARNING) << "Ad window is too short for start " << startSecs;
        return false;
      }

    Database::IdT bId;
    if (!IdFromJson (terms["b"], bId))
      return false;
    const auto b = jc.buildings.GetById (bId);
    if (b == nullptr || b->GetProto ().foundation ()
          || b->GetFaction () == Faction::ANCIENT)
      {
        LOG (WARNING) << "Invalid ad-slot building " << bId;
        return false;
      }
    if (b->GetOwner () == poster.GetName ())
      {
        LOG (WARNING) << "Cannot rent an ad slot on one's own building";
        return false;
      }

    return true;
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    Database::IdT bId;
    CHECK (IdFromJson (terms["b"], bId));
    job.SetLinkedId (bId);

    auto* ap = job.MutableProto ().mutable_ad ();
    ap->set_slot (terms["slot"].asUInt ());
    ap->set_content_hash (terms["hash"].asString ());
    if (terms["start"].isInt64 () && terms["start"].asInt64 () > 0)
      ap->set_start (jc.ctx.Timestamp () + terms["start"].asInt64 ());

    /* The payee is the building's owner at post time; their accept is the
       approval.  ValidateAccept re-verifies ownership at accept time in case
       the building changed hands in between.  */
    const auto b = jc.buildings.GetById (bId);
    CHECK (b != nullptr);
    job.MutableProto ().set_designated_worker (b->GetOwner ());
  }

  bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const override
  {
    const auto b = jc.buildings.GetById (job.GetLinkedId ());
    if (b == nullptr || b->GetOwner () != worker.GetName ())
      {
        LOG (WARNING)
            << worker.GetName () << " no longer owns the ad-slot building of "
            << "job " << job.GetId ();
        return false;
      }

    /* Slot exclusivity: the accept is the booking, so it is rejected while
       another ACCEPTED ad on the same (building, slot) overlaps this ad's
       window.  Windows are half-open [start, deadline): back-to-back
       bookings may share an endpoint.  The candidate's window is clamped to
       now (an ad accepted after its start renders from the accept), which
       also skips competitors already due for this block's expiry sweep.
       An unset start means the window opened at post.  OPEN ads never block
       each other -- the owner's accept picks the winner.  */
    const auto& ap = job.GetProto ().ad ();
    const int64_t now = jc.ctx.Timestamp ();
    const int64_t candLo = std::max<int64_t> (ap.start (), now);
    const int64_t candHi = job.GetDeadline ();

    /* The accept is the booking and the payout is always the full rent, so
       the approval must leave at least the minimum window actually
       displayable -- otherwise a last-second approval would collect the
       whole rent for (nearly) no display time.  An unapproved ad simply
       voids at its deadline and the advertiser's escrow refunds.  Accepting
       before a scheduled start always passes: post validation floors the
       booked window itself.  */
    if (candHi - candLo < jc.ctx.RoConfig ()->params ().min_work_window ())
      {
        LOG (WARNING)
            << "Ad job " << job.GetId ()
            << " has too little of its window left to accept";
        return false;
      }
    /* The rows are read straight off the result (never through GetFromResult):
       the caller already holds the handle of the job being accepted, which
       this query returns as well.  */
    auto res = jc.jobs.QueryForLinkedId (job.GetLinkedId ());
    while (res.Step ())
      {
        const auto otherId
            = static_cast<Database::IdT> (res.Get<JobResult::id> ());
        if (otherId == job.GetId ()
              || static_cast<Job::Type> (res.Get<JobResult::type> ())
                  != Job::Type::AD
              || static_cast<Job::Status> (res.Get<JobResult::status> ())
                  != Job::Status::ACCEPTED)
          continue;
        const auto otherData = res.GetProto<JobResult::proto> ();
        const auto& op = otherData.Get ().ad ();
        if (op.slot () != ap.slot ())
          continue;
        /* Ads are always deadlined, but never read a NULL deadline column as
           0 (it would silently defeat the overlap test); skip defensively.  */
        if (res.IsNull<JobResult::deadline> ())
          continue;
        if (candLo < res.Get<JobResult::deadline> () && op.start () < candHi)
          {
            LOG (WARNING)
                << "Ad job " << job.GetId () << " overlaps accepted ad "
                << otherId << " on building "
                << job.GetLinkedId () << " slot " << ap.slot ();
            return false;
          }
      }

    return true;
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    if (job.GetStatus () == Job::Status::ACCEPTED)
      {
        /* The rented period elapsed: the rent pays the owner.  */
        LOG (INFO)
            << "Ad job " << job.GetId () << " completed its period; paying "
            << job.GetReward () << " to " << job.GetWorker ();
        SettleSuccessAtHook (jc, job);
        return JobOutcome::COMPLETED;
      }
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

  JobOutcome
  OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const override
  {
    /* The building died mid-period: the advertiser gets the rent back.  */
    LOG (INFO)
        << "Voided ad job " << job.GetId () << ": building "
        << job.GetLinkedId () << " destroyed";
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

  bool
  OnLinkedBuildingTransferred (const JobContext& jc, Job& job) const override
  {
    /* A sale is the destruction case for an ad: the new owner never approved
       the content and the old owner is no longer the payee, so the advertiser
       gets the rent back.  This voids OPEN ads too -- their designated worker
       is the old owner, who could never pass the accept-time ownership
       re-check anyway.  */
    LOG (INFO)
        << "Voided ad job " << job.GetId () << ": building "
        << job.GetLinkedId () << " sold";
    VoidJobAtHook (jc, job);
    return true;
  }

};

/* ************************************************************************** */
/* Toll.                                                                      */

/**
 * Safe-passage toll: the poster (traveller) escrows the toll for the
 * designated gatekeeper.  Self-enforcing non-aggression: the gatekeeper is
 * paid only if the traveller's character (the linked_id) survives the
 * window; if it dies -- by anyone's hand -- the toll refunds.
 */
class TollPredicate : public HookSettledPredicate
{

public:

  bool
  RequiresApproval () const override
  {
    return true;
  }

  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::CHARACTER;
  }

  Faction
  AudienceFaction (const Account& poster) const override
  {
    return Faction::INVALID;
  }

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    if (!RequireZeroCollateral (terms, "A toll"))
      return false;

    Database::IdT chId;
    if (!ValidateOwnedCharacter (jc, terms, "ch", poster, "Toll traveller",
                                 chId))
      return false;

    if (!terms["w"].isString ())
      return false;
    const std::string gatekeeper = terms["w"].asString ();
    if (gatekeeper == poster.GetName ())
      return false;
    const auto w = jc.accounts.GetByName (gatekeeper);
    if (w == nullptr || !w->IsInitialised ())
      {
        LOG (WARNING) << "Invalid toll gatekeeper: " << gatekeeper;
        return false;
      }

    return true;
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    Database::IdT chId;
    CHECK (IdFromJson (terms["ch"], chId));
    job.SetLinkedId (chId);
    job.MutableProto ().mutable_toll ();
    job.MutableProto ().set_designated_worker (terms["w"].asString ());
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    if (job.GetStatus () == Job::Status::ACCEPTED)
      {
        /* Kills run first, so the traveller survived the window: the
           gatekeeper collects.  */
        LOG (INFO)
            << "Toll job " << job.GetId () << " window elapsed; paying "
            << job.GetReward () << " to " << job.GetWorker ();
        SettleSuccessAtHook (jc, job);
        return JobOutcome::COMPLETED;
      }
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

  JobOutcome
  OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const override
  {
    /* The traveller died inside the window: the toll refunds -- the
       gatekeeper's protection failed, whoever fired the shot.  */
    LOG (INFO)
        << "Voided toll job " << job.GetId () << ": traveller "
        << job.GetLinkedId () << " died";
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

};

/* ************************************************************************** */

/** The process-lifetime predicate singletons.  */
const TransportPredicate TRANSPORT_PREDICATE;
const HaulPredicate HAUL_PREDICATE;
const WantedPredicate WANTED_PREDICATE;
const ProtectPredicate PROTECT_PREDICATE;
const DestroyPredicate DESTROY_PREDICATE;
const EscortPredicate ESCORT_PREDICATE;
const BodyguardPredicate BODYGUARD_PREDICATE;
const PatrolPredicate PATROL_PREDICATE;
const RentalPredicate RENTAL_PREDICATE;
const AdPredicate AD_PREDICATE;
const TollPredicate TOLL_PREDICATE;

/**
 * The one place a job type is registered: enum value, move/JSON name and
 * predicate object.  Every lookup (name -> type, type -> predicate,
 * type -> name) derives from this table, so a new type cannot be half-wired.
 */
struct JobTypeEntry
{
  Job::Type type;
  const char* name;
  const JobPredicate* predicate;
};

const JobTypeEntry JOB_TYPE_REGISTRY[] =
  {
    {Job::Type::TRANSPORT, "transport", &TRANSPORT_PREDICATE},
    {Job::Type::HAUL, "haul", &HAUL_PREDICATE},
    {Job::Type::WANTED, "wanted", &WANTED_PREDICATE},
    {Job::Type::PROTECT, "protect", &PROTECT_PREDICATE},
    {Job::Type::DESTROY, "destroy", &DESTROY_PREDICATE},
    {Job::Type::ESCORT, "escort", &ESCORT_PREDICATE},
    {Job::Type::BODYGUARD, "bodyguard", &BODYGUARD_PREDICATE},
    {Job::Type::PATROL, "patrol", &PATROL_PREDICATE},
    {Job::Type::RENTAL, "rental", &RENTAL_PREDICATE},
    {Job::Type::AD, "ad", &AD_PREDICATE},
    {Job::Type::TOLL, "toll", &TOLL_PREDICATE},
  };

} // anonymous namespace

Job::Type
JobTypeFromString (const std::string& name)
{
  for (const auto& entry : JOB_TYPE_REGISTRY)
    if (name == entry.name)
      return entry.type;
  return Job::Type::INVALID;
}

const JobPredicate*
PredicateForType (const Job::Type type)
{
  for (const auto& entry : JOB_TYPE_REGISTRY)
    if (type == entry.type)
      return entry.predicate;
  return nullptr;
}

const char*
JobTypeName (const Job::Type type)
{
  for (const auto& entry : JOB_TYPE_REGISTRY)
    if (type == entry.type)
      return entry.name;
  return nullptr;
}

const char*
JobOutcomeName (const JobOutcome outcome)
{
  switch (outcome)
    {
    case JobOutcome::COMPLETED:
      return "completed";
    case JobOutcome::FAILED:
      return "failed";
    case JobOutcome::CANCELLED:
      return "cancelled";
    case JobOutcome::VOID:
      return "void";
    case JobOutcome::DRAINED:
      return "drained";
    default:
      return nullptr;
    }
}

/* ************************************************************************** */
/* The generic move-op lifecycle.                                             */

namespace
{

/**
 * POST: locks the reward + burns the posting fee and creates an OPEN job with
 * the type-specific payload (via the predicate).  Generic across job types.
 */
class PostOperation : public JobOperation
{

private:

  const Job::Type type;

  /** The relative deadline in seconds; -1 = standing (no deadline).  */
  const int64_t deadlineSecs;

  /** The work window in seconds; -1 = not given.  */
  const int64_t workWindowSecs;

  const Amount reward;
  const Amount collateral;

  /** The full raw post object; the predicate reads its type terms.  */
  const Json::Value terms;

  /** Computes the burned posting fee for the reward.  */
  Amount
  Fee () const
  {
    const auto& p = jc.ctx.RoConfig ()->params ();
    const Amount proportional = reward * p.job_post_fee_bps () / 10000;
    return std::max<Amount> (p.job_post_fee_min (), proportional);
  }

public:

  PostOperation (Account& a, const JobContext& c, const Job::Type t,
                 const int64_t d, const int64_t wd, const Amount rew,
                 const Amount col, const Json::Value& tm)
    : JobOperation(a, c), type(t), deadlineSecs(d), workWindowSecs(wd),
      reward(rew), collateral(col), terms(tm)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
PostOperation::IsValid () const
{
  if (account.GetFaction () == Faction::INVALID)
    {
      LOG (WARNING)
          << account.GetName () << " has no faction, cannot post a job";
      return false;
    }

  /* The reward must be strictly positive:  a zero-reward job would still
     bump the worker's jobs_completed counter on settlement, farming the
     count-based half of the reputation for just the posting fee -- exactly
     the free credit the wanted payout refuses when a tranche share rounds
     to zero.  Collateral MAY be zero (many types require that).  */
  if (reward <= 0 || collateral < 0)
    return false;

  const auto* pred = PredicateForType (type);
  CHECK (pred != nullptr);

  /* The standing class is posted without a deadline and only the standing
     types may be; everything else has a bounded one.  */
  const bool standing = (deadlineSecs < 0);
  if (standing != pred->IsStanding ())
    {
      LOG (WARNING)
          << "Job type does not match the "
          << (standing ? "missing" : "given") << " deadline";
      return false;
    }
  const auto& p = jc.ctx.RoConfig ()->params ();
  if (!standing)
    {
      /* Ads are exempt from the listing floor: their deadline is the end of
         the rented calendar window, bounded below by the predicate's
         window-length check instead.  The cap applies to everyone (for ads
         it is the booking horizon).  */
      if (pred->UsesWorkWindow () && deadlineSecs < p.min_listing_window ())
        {
          LOG (WARNING) << "Job listing window too short: " << deadlineSecs;
          return false;
        }
      if (deadlineSecs > p.max_listing_window ())
        {
          LOG (WARNING) << "Job listing window too long: " << deadlineSecs;
          return false;
        }
    }

  /* The work window is required exactly where it is used (every deadlined
     type except ads); a missing one (-1) fails the floor.  Elsewhere it
     would be dead data on the job, so it is rejected.  */
  if (pred->UsesWorkWindow ())
    {
      if (workWindowSecs < p.min_work_window ()
            || workWindowSecs > p.max_work_window ())
        {
          LOG (WARNING)
              << "Job work window missing or out of range: " << workWindowSecs;
          return false;
        }
    }
  else if (workWindowSecs >= 0)
    {
      LOG (WARNING) << "Job type does not take a work window";
      return false;
    }

  if (!pred->ValidatePost (jc, account, terms))
    return false;

  const Amount fee = Fee ();
  if (account.GetBalance () < reward + fee)
    {
      LOG (WARNING)
          << account.GetName () << " cannot afford reward " << reward
          << " + fee " << fee << " (balance " << account.GetBalance () << ")";
      return false;
    }

  return true;
}

void
PostOperation::Execute ()
{
  const Amount fee = Fee ();

  LOG (INFO)
      << account.GetName () << " posting a job (reward " << reward
      << ", collateral " << collateral << ", fee " << fee << ")";

  /* Escrow the reward (tracked in the job row, released on settlement) and
     burn the posting fee (removed from circulation, never credited).  */
  LockJobCoins (account, reward);
  account.AddBalance (-fee);

  const auto* pred = PredicateForType (type);
  auto job = jc.jobs.CreateNew (type, pred->AudienceFaction (account),
                                account.GetName (), reward, collateral);
  if (deadlineSecs >= 0)
    job->SetDeadline (jc.ctx.Timestamp () + deadlineSecs);
  if (workWindowSecs >= 0)
    job->MutableProto ().set_work_window (workWindowSecs);
  pred->ApplyPost (jc, account, terms, *job);
}

/* ************************************************************************** */

/**
 * ASSIGN: the poster names (or changes) the designated worker before anyone
 * has accepted.  Generic across job types.
 */
class AssignOperation : public JobOperation
{

private:

  const Database::IdT jobId;
  const std::string designated;

public:

  AssignOperation (Account& a, const JobContext& c, const Database::IdT id,
                   const std::string& w)
    : JobOperation(a, c), jobId(id), designated(w)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
AssignOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (jobId);
  if (job == nullptr || job->GetStatus () != Job::Status::OPEN)
    {
      LOG (WARNING) << "Job " << jobId << " not open to assign";
      return false;
    }
  if (job->GetPoster () != account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " does not own job " << jobId;
      return false;
    }
  /* Assignment is exclusive-only (design 3.4):  a standing job (e.g. a
     wanted board) is never accepted by a single worker, so a
     designated_worker on it would be dead data polluting the public JSON.
     The type decides, not the deadline column -- a notice-cancelled standing
     job carries a deadline but is still standing.  */
  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);
  if (pred->IsStanding ())
    {
      LOG (WARNING)
          << "Job " << jobId << " is standing; cannot designate a worker";
      return false;
    }
  if (designated == account.GetName ())
    {
      LOG (WARNING) << "Cannot designate oneself as worker for job " << jobId;
      return false;
    }

  const auto w = jc.accounts.GetByName (designated);
  if (w == nullptr || !w->IsInitialised ())
    {
      LOG (WARNING) << "Designated worker " << designated << " does not exist";
      return false;
    }
  /* The designated worker must match the audience faction (INVALID audience =
     any faction).  */
  if (job->GetFaction () != Faction::INVALID
        && w->GetFaction () != job->GetFaction ())
    {
      LOG (WARNING)
          << "Designated worker " << designated
          << " is the wrong faction for job " << jobId;
      return false;
    }

  return true;
}

void
AssignOperation::Execute ()
{
  auto job = jc.jobs.GetById (jobId);
  CHECK (job != nullptr) << "Job disappeared: " << jobId;
  LOG (INFO)
      << account.GetName () << " assigning job " << jobId << " to "
      << designated;
  job->MutableProto ().set_designated_worker (designated);
}

/* ************************************************************************** */

/**
 * ACCEPT: the worker locks their collateral and binds the job to them.  The
 * designated-worker and approval rules are enforced here, and the predicate
 * can add type checks (ValidateAccept) and side effects (OnAccept).
 */
class AcceptOperation : public JobOperation
{

private:

  const Database::IdT jobId;

public:

  AcceptOperation (Account& a, const JobContext& c, const Database::IdT id)
    : JobOperation(a, c), jobId(id)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
AcceptOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (jobId);
  if (job == nullptr || job->GetStatus () != Job::Status::OPEN)
    {
      LOG (WARNING) << "Job " << jobId << " not open to accept";
      return false;
    }

  /* An expired listing is the sweep's to void (see JobIsDue).  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Job " << jobId << " is at or past its deadline; cannot accept";
      return false;
    }

  if (job->GetPoster () == account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " cannot accept own job " << jobId;
      return false;
    }

  /* Audience faction: INVALID audience means any faction may accept.  */
  if (job->GetFaction () != Faction::INVALID
        && job->GetFaction () != account.GetFaction ())
    {
      LOG (WARNING)
          << account.GetName () << " is the wrong faction for job " << jobId;
      return false;
    }

  /* Designated-worker / approval gate.  */
  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);
  const std::string& designated = job->GetProto ().designated_worker ();
  if (pred->RequiresApproval ())
    {
      if (designated.empty () || designated != account.GetName ())
        {
          LOG (WARNING)
              << "Job " << jobId << " requires approval; "
              << account.GetName () << " is not the designated worker";
          return false;
        }
    }
  else if (!designated.empty () && designated != account.GetName ())
    {
      LOG (WARNING)
          << "Job " << jobId << " is designated to " << designated
          << ", not " << account.GetName ();
      return false;
    }

  /* Type-specific accept checks (open-claim rejection, rental stock,
     ad-slot ownership).  */
  if (!pred->ValidateAccept (jc, *job, account))
    return false;

  if (account.GetBalance () < job->GetCollateral ())
    {
      LOG (WARNING)
          << account.GetName () << " cannot afford collateral "
          << job->GetCollateral () << " for job " << jobId;
      return false;
    }

  return true;
}

void
AcceptOperation::Execute ()
{
  auto job = jc.jobs.GetById (jobId);
  CHECK (job != nullptr) << "Job disappeared: " << jobId;

  LOG (INFO)
      << account.GetName () << " accepting job " << jobId
      << ", locking collateral " << job->GetCollateral ();

  LockJobCoins (account, job->GetCollateral ());
  job->SetWorker (account.GetName ());
  job->SetStatus (Job::Status::ACCEPTED);

  /* The work clock starts now: the deadline (so far the listing expiry) is
     rewritten so the worker gets the full work window from the moment they
     commit, no matter how late in the listing they accept.  Ads have no
     work window and keep their posted calendar deadline.  */
  const auto& pb = job->GetProto ();
  if (pb.has_work_window ())
    job->SetDeadline (jc.ctx.Timestamp () + pb.work_window ());

  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);
  pred->OnAccept (jc, *job, account);
}

/* ************************************************************************** */

/**
 * CANCEL: the poster withdraws an OPEN job before anyone accepts, and the
 * reward is refunded (the posting fee is not).  On a standing job the cancel
 * is a NOTICE instead: the job stays on the board with a deadline set
 * bounty_cancel_notice out, kills during the window still pay, and only the
 * unearned remainder refunds when the notice expires.
 */
class CancelOperation : public JobOperation
{

private:

  const Database::IdT jobId;

public:

  CancelOperation (Account& a, const JobContext& c, const Database::IdT id)
    : JobOperation(a, c), jobId(id)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
CancelOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (jobId);
  if (job == nullptr || job->GetStatus () != Job::Status::OPEN)
    {
      LOG (WARNING) << "Job " << jobId << " not open to cancel";
      return false;
    }
  if (job->GetPoster () != account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " does not own job " << jobId
          << " to cancel";
      return false;
    }

  /* A standing job that already has a deadline is already closing: no
     window-pushing and no reopen (repost instead).  */
  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);
  if (pred->IsStanding () && job->HasDeadline ())
    {
      LOG (WARNING) << "Standing job " << jobId << " is already closing";
      return false;
    }

  return true;
}

void
CancelOperation::Execute ()
{
  auto job = jc.jobs.GetById (jobId);
  CHECK (job != nullptr) << "Job disappeared: " << jobId;

  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);

  if (pred->IsStanding ())
    {
      /* Notice-cancel: convert the standing job into a normal expiring one;
         the existing deadline sweep does the eventual refund.  */
      const int64_t notice
          = jc.ctx.RoConfig ()->params ().bounty_cancel_notice ();
      job->SetDeadline (jc.ctx.Timestamp () + notice);
      LOG (INFO)
          << account.GetName () << " gave notice on standing job " << jobId
          << "; it closes at " << job->GetDeadline ();
      return;
    }

  const Amount reward = job->GetReward ();

  LOG (INFO)
      << account.GetName () << " cancelling job " << jobId
      << ", refunding reward " << reward;

  pred->OnCancel (jc, *job);

  ReleaseJobCoins (account, reward);
  SettleAndDelete (jc.jobs, std::move (job), JobOutcome::CANCELLED, jc.ctx);
}

/* ************************************************************************** */

/**
 * FULFIL: the executor (the worker, or the poster for PosterFulfils types)
 * submits the type-specific fulfil op; the predicate verifies and settles.
 * Generic dispatch.
 */
class FulfilOperation : public JobOperation
{

private:

  const Database::IdT jobId;

  /** The full raw fulfil object; the predicate reads its type args.  */
  const Json::Value args;

public:

  FulfilOperation (Account& a, const JobContext& c, const Database::IdT id,
                   const Json::Value& ar)
    : JobOperation(a, c), jobId(id), args(ar)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
FulfilOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (jobId);
  if (job == nullptr || job->GetStatus () != Job::Status::ACCEPTED)
    {
      LOG (WARNING) << "Job " << jobId << " not accepted to fulfil";
      return false;
    }

  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);

  const std::string& executor
      = pred->PosterFulfils () ? job->GetPoster () : job->GetWorker ();
  if (executor != account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " may not fulfil job " << jobId;
      return false;
    }

  /* An expired job is the sweep's to settle as FAILED (see JobIsDue).  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Job " << jobId << " is at or past its deadline; cannot fulfil";
      return false;
    }

  return pred->CanFulfil (jc, *job, account, args);
}

void
FulfilOperation::Execute ()
{
  auto job = jc.jobs.GetById (jobId);
  CHECK (job != nullptr) << "Job disappeared: " << jobId;

  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);
  const FulfilResult result = pred->DoFulfil (jc, *job, account, args);

  if (result == FulfilResult::COMPLETE)
    {
      SettleAndDelete (jc.jobs, std::move (job), JobOutcome::COMPLETED,
                       jc.ctx);
    }
  /* PROGRESS: the mutated proto persists when the handle destructs.  */
}

/* ************************************************************************** */

} // anonymous namespace

std::unique_ptr<JobOperation>
JobOperation::Parse (Account& acc, const Json::Value& data,
                     const JobContext& jc)
{
  if (!data.isObject ())
    return nullptr;

  /* Exactly one of the five discriminator keys must be present.  */
  const bool hasT = data.isMember ("t");
  const bool hasS = data.isMember ("s");
  const bool hasA = data.isMember ("a");
  const bool hasC = data.isMember ("c");
  const bool hasF = data.isMember ("f");
  if (hasT + hasS + hasA + hasC + hasF != 1)
    return nullptr;

  std::unique_ptr<JobOperation> op;

  if (hasT)
    {
      /* POST: {"t":<type>,"d":<secs>,"wd":<secs>,"r":<reward>,
         "co":<collateral>,...}.  The listing deadline "d" is required
         except for the standing types (wanted), which must omit it; the
         work window "wd" is required exactly for the types that use it
         (every deadlined one except ads) -- both checked against the type
         in IsValid.  */
      if (!data["t"].isString ())
        return nullptr;
      const Job::Type type = JobTypeFromString (data["t"].asString ());
      if (type == Job::Type::INVALID || PredicateForType (type) == nullptr)
        return nullptr;
      const auto relativeSecs = [&data] (const char* key, int64_t& out)
        {
          out = -1;
          if (!data.isMember (key))
            return true;
          if (!data[key].isInt64 () || !xaya::IsIntegerValue (data[key]))
            return false;
          out = data[key].asInt64 ();
          return out >= 0;
        };
      int64_t deadlineSecs, workWindowSecs;
      if (!relativeSecs ("d", deadlineSecs)
            || !relativeSecs ("wd", workWindowSecs))
        return nullptr;
      Amount reward, collateral;
      if (!CoinAmountFromJson (data["r"], reward)
            || !CoinAmountFromJson (data["co"], collateral))
        return nullptr;
      op = std::make_unique<PostOperation> (acc, jc, type, deadlineSecs,
                                            workWindowSecs, reward,
                                            collateral, data);
    }
  else if (hasS)
    {
      /* ASSIGN: {"s":<id>,"w":<account>} -- exactly those two members.  */
      if (data.size () != 2)
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["s"], id) || !data["w"].isString ())
        return nullptr;
      op = std::make_unique<AssignOperation> (acc, jc, id,
                                              data["w"].asString ());
    }
  else if (hasA)
    {
      /* ACCEPT: {"a":<id>}.  */
      if (data.size () != 1)
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["a"], id))
        return nullptr;
      op = std::make_unique<AcceptOperation> (acc, jc, id);
    }
  else if (hasC)
    {
      /* CANCEL: {"c":<id>}.  */
      if (data.size () != 1)
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["c"], id))
        return nullptr;
      op = std::make_unique<CancelOperation> (acc, jc, id);
    }
  else
    {
      /* FULFIL: {"f":<id>} or {"f":<id>,"ch":<charId>}.  */
      CHECK (hasF);
      if (data.size () > 2 || (data.size () == 2 && !data.isMember ("ch")))
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["f"], id))
        return nullptr;
      op = std::make_unique<FulfilOperation> (acc, jc, id, data);
    }

  return op;
}

/* ************************************************************************** */
/* Per-block hooks (confirmed processing only).                               */

namespace
{

/**
 * Locally-constructed table handles for a block hook plus the JobContext they
 * form.  The hooks are standalone (not nested inside the move processor's
 * handles), so they own their table handles for the duration of the call.
 */
struct BlockHookTables
{
  AccountsTable accounts;
  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;
  CharacterTable characters;
  OngoingsTable ongoings;
  GroundLootTable groundLoot;
  JobsTable jobs;

  explicit BlockHookTables (Database& db)
    : accounts(db), buildings(db), buildingInv(db), characters(db),
      ongoings(db), groundLoot(db), jobs(db)
  {}

  JobContext
  MakeContext (const pxd::Context& ctx)
  {
    return {ctx, accounts, buildings, buildingInv, characters, ongoings,
            groundLoot, jobs};
  }
};

} // anonymous namespace

void
ExpireJobs (Database& db, const Context& ctx)
{
  JobsTable jobs(db);

  /* The deterministic retention prune for the settled-jobs history: on most
     blocks an indexed no-op, and a negative cutoff early in a chain's life
     simply matches nothing.  An unset retention (0) means keep forever
     rather than keep nothing.  */
  const int64_t retention
      = ctx.RoConfig ()->params ().jobs_history_retention ();
  if (retention > 0)
    jobs.PruneHistory (ctx.Timestamp () - retention);

  /* Snapshot the due jobs (fully consuming the query) before mutating any
     balances or deleting rows.  On idle blocks this is an indexed no-op.  */
  std::vector<Database::IdT> due;
  {
    auto res = jobs.QueryForDeadline (ctx.Timestamp ());
    while (res.Step ())
      {
        auto j = jobs.GetFromResult (res);
        CHECK (j->HasDeadline ());
        CHECK_LE (j->GetDeadline (), ctx.Timestamp ())
            << "Job " << j->GetId () << " with a future deadline in expiry";
        due.push_back (j->GetId ());
      }
  }

  if (due.empty ())
    return;

  BlockHookTables tables(db);
  const JobContext jc = tables.MakeContext (ctx);
  for (const auto id : due)
    {
      auto j = tables.jobs.GetById (id);
      CHECK (j != nullptr);
      const auto* pred = PredicateForType (j->GetType ());
      CHECK (pred != nullptr);
      const JobOutcome outcome = pred->OnExpire (jc, *j);
      SettleAndDelete (tables.jobs, std::move (j), outcome, ctx);
    }
}

namespace
{

/**
 * Shared skeleton for the linked-entity settlement hooks.  Snapshots the jobs
 * linked to an entity -- fully draining the query before any row or balance is
 * touched -- then runs `resolve` for each in turn.  `resolve` returns the
 * outcome to record (which then writes history and deletes the row), or
 * std::nullopt to leave the job on the board untouched.
 */
template <typename Resolve>
  void
  SettleLinkedJobs (Database& db, const Context& ctx,
                    const Database::IdT entityId, Resolve resolve)
{
  JobsTable jobs(db);

  std::vector<Database::IdT> affected;
  {
    auto res = jobs.QueryForLinkedId (entityId);
    while (res.Step ())
      affected.push_back (jobs.GetFromResult (res)->GetId ());
  }

  if (affected.empty ())
    return;

  BlockHookTables tables(db);
  const JobContext jc = tables.MakeContext (ctx);
  for (const auto id : affected)
    {
      auto j = tables.jobs.GetById (id);
      CHECK (j != nullptr);
      const auto* pred = PredicateForType (j->GetType ());
      CHECK (pred != nullptr);
      const std::optional<JobOutcome> outcome = resolve (jc, *pred, *j);
      if (!outcome.has_value ())
        continue;
      SettleAndDelete (tables.jobs, std::move (j), *outcome, ctx);
    }
}

} // anonymous namespace

void
OnJobEntityDestroyed (Database& db, const Context& ctx,
                      const Database::IdT entityId)
{
  SettleLinkedJobs (db, ctx, entityId,
    [] (const JobContext& jc, const JobPredicate& pred, Job& job)
        -> std::optional<JobOutcome>
      {
        return pred.OnLinkedEntityDestroyed (jc, job);
      });
}

void
OnJobBuildingTransferred (Database& db, const Context& ctx,
                          const Database::IdT buildingId)
{
  SettleLinkedJobs (db, ctx, buildingId,
    [] (const JobContext& jc, const JobPredicate& pred, Job& job)
        -> std::optional<JobOutcome>
      {
        if (!pred.OnLinkedBuildingTransferred (jc, job))
          return std::nullopt;
        return JobOutcome::VOID;
      });
}

/* ************************************************************************** */

JobsBountyTracker::JobsBountyTracker (Database& d, const Context& c,
                                      const DamageLists& l)
  : db(d), ctx(c), dl(l), characters(d)
{
  bountyNames = JobsTable (db).GetActiveBountyNames ();
}

void
JobsBountyTracker::UpdateForKill (const proto::TargetId& target)
{
  if (target.type () != proto::TargetId::TYPE_CHARACTER)
    return;
  if (bountyNames.empty ())
    return;

  /* The pre-removal pass: the victim's row (and the damage lists) are still
     live here.  The common path is one hash lookup.  */
  std::string victimOwner;
  {
    const auto victim = characters.GetById (target.id ());
    CHECK (victim != nullptr);
    victimOwner = victim->GetOwner ();
  }
  if (bountyNames.count (victimOwner) == 0)
    return;

  /* Derive the distinct killer accounts, exactly like fame does.  Kills with
     an empty owner set (e.g. turret-only damage, which is not damage-listed)
     pay nothing and consume nothing.  */
  std::set<std::string> owners;
  for (const auto attackerId : dl.GetAttackers (target.id ()))
    {
      auto c = characters.GetById (attackerId);
      CHECK (c != nullptr);
      owners.insert (c->GetOwner ());
    }
  if (owners.empty ())
    return;

  /* Pay one tranche per pool on this name (stacked bounties all pay).  The
     name set is a block-start snapshot, so it can be stale within the block:
     if an earlier kill in this very block drained and deleted the last pool
     on the name, the query comes back empty -- drop the name and move on
     (this must NOT be a CHECK, or a target losing more characters than the
     pool has tranches in one block would halt every node).  */
  std::vector<Database::IdT> pools;
  {
    JobsTable jobs(db);
    auto res = jobs.QueryForLinkedName (victimOwner);
    while (res.Step ())
      pools.push_back (jobs.GetFromResult (res)->GetId ());
  }
  if (pools.empty ())
    {
      bountyNames.erase (victimOwner);
      return;
    }

  BlockHookTables tables(db);
  const JobContext jc = tables.MakeContext (ctx);
  for (const auto id : pools)
    {
      auto j = tables.jobs.GetById (id);
      CHECK (j != nullptr);
      const auto* pred = PredicateForType (j->GetType ());
      CHECK (pred != nullptr);
      /* Only types that actually settle on kills take a payout; a future
         type reusing linked_name for something else is left alone.  */
      if (!pred->SettlesOnTargetKill ())
        continue;
      const bool drained = pred->OnTargetKill (jc, *j, owners);
      if (drained)
        SettleAndDelete (tables.jobs, std::move (j), JobOutcome::DRAINED, ctx);
      /* Not drained: the mutated pool flushes when the handle destructs.  */
    }
}

/* ************************************************************************** */

void
ValidateJobs (Database& db)
{
  AccountsTable accounts(db);
  BuildingsTable buildings(db);
  CharacterTable characters(db);
  JobsTable jobs(db);

  auto res = jobs.QueryAll ();
  while (res.Step ())
    {
      auto j = jobs.GetFromResult (res);
      const auto id = j->GetId ();

      const auto* pred = PredicateForType (j->GetType ());
      CHECK (pred != nullptr) << "Job " << id << " has an unknown type";

      {
        const auto poster = accounts.GetByName (j->GetPoster ());
        CHECK (poster != nullptr && poster->IsInitialised ())
            << "Job " << id << " has an invalid poster";
      }

      switch (j->GetStatus ())
        {
        case Job::Status::OPEN:
          CHECK (j->GetWorker ().empty ())
              << "Open job " << id << " has a worker";
          break;
        case Job::Status::ACCEPTED:
          {
            CHECK (!j->GetWorker ().empty ())
                << "Accepted job " << id << " has no worker";
            const auto worker = accounts.GetByName (j->GetWorker ());
            CHECK (worker != nullptr)
                << "Job " << id << " has an invalid worker";
          }
          break;
        default:
          LOG (FATAL) << "Job " << id << " has an invalid status";
        }

      /* Only the standing types may lack a deadline (a closing standing job
         has one, which is fine).  */
      if (!j->HasDeadline ())
        CHECK (pred->IsStanding ())
            << "Non-standing job " << id << " has no deadline";

      if (!j->GetLinkedName ().empty ())
        CHECK (accounts.GetByName (j->GetLinkedName ()) != nullptr)
            << "Job " << id << " targets an unknown account";

      /* The linked entity, when set, must exist and be of the kind the
         type's predicate declares -- derived, not a parallel type list.  */
      const auto linked = j->GetLinkedId ();
      switch (pred->LinkedEntityKind ())
        {
        case JobLinkedKind::BUILDING:
          CHECK (linked != Database::EMPTY_ID
                    && buildings.GetById (linked) != nullptr)
              << "Job " << id << " links to an unknown building";
          break;
        case JobLinkedKind::CHARACTER:
          CHECK (linked != Database::EMPTY_ID
                    && characters.GetById (linked) != nullptr)
              << "Job " << id << " links to an unknown character";
          break;
        case JobLinkedKind::NONE:
          CHECK_EQ (linked, Database::EMPTY_ID)
              << "Job " << id << " should not have a linked entity";
          break;
        }
    }
}

} // namespace pxd
