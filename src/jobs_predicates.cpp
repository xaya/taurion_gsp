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

#include "database/params.hpp"

#include "hexagonal/coord.hpp"

#include <xayautil/jsonutils.hpp>

#include <glog/logging.h>

#include <algorithm>
#include <limits>
#include <map>
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
 * Bumps the per-account completion counters: consensus-stored vetting
 * signals that no consensus rule ever reads back -- clients surface them
 * so posters can vet applicants and workers can vet posters.
 *
 * The increments are deliberately unchecked at the actual field widths.
 * The counts are uint32, so wrapping one takes 2^32 (~4.3e9) settlements,
 * each burning a posting fee; the value counter is uint64 and grows by at
 * most MAX_COIN_AMOUNT (1e11) per settlement, so wrapping it takes ~1.8e8
 * max-value jobs, each escrowing that value and burning its fee.  Both are
 * economically unreachable, and a wrap would in any case be deterministic
 * across nodes (no split risk).  Saturation is deliberately NOT used: it
 * would add consensus-visible code for an unreachable boundary.  Raw
 * counts are a display-only signal that colluding pairs can inflate for
 * the fee price; the value-weighted counter is the stronger, fee-backed
 * face-value signal -- a colluding pair can still buy credit on it, but
 * only at the posting-fee burn (~1% of the face value credited), so it
 * anchors to burned cost rather than proving arm's-length work.  Any
 * future consumer of these stats must weigh by value (or add thresholds)
 * rather than trust counts.
 *
 * `times` folds several settlements into one call (the aggregated
 * wanted-pool payout, where one kill completes `times` pools at once);
 * `value` is always the TOTAL across them.
 */
void
BumpJobStats (Account& a, const bool completed, const Amount value,
              const unsigned times = 1)
{
  auto& pb = a.MutableProto ();
  if (completed)
    {
      pb.set_jobs_completed (pb.jobs_completed () + times);
      pb.set_jobs_value_completed (pb.jobs_value_completed () + value);
    }
  else
    pb.set_jobs_failed (pb.jobs_failed () + times);
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"items", "to"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    static const std::vector<const char*> keys = {"to"};
    return keys;
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"items", "from", "to"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    /* Both ends: linked to the source while OPEN, swapped to the
       destination at accept.  */
    static const std::vector<const char*> keys = {"from", "to"};
    return keys;
  }

  Database::IdT
  AcceptRelinkId (const Job& job) const override
  {
    /* Accept swaps the link to the destination, so it must pass the
       per-entity admission gate then (the POST count cannot see it).  */
    return job.GetProto ().haul ().dest_building ();
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
/* Target-kill family (wanted + assassination).                               */

/**
 * Shared base for the kill contracts on an account name (the linked_name):
 * the escrowed reward is a pool of `quota` equal tranches of reward/quota,
 * consumed one per qualifying kill of the target's characters at the kill
 * hook.  Both types share the POST grammar ({name, n}), the bounty reward
 * floor and the per-target stacked-listings cap; they differ in duration
 * class and in who may collect: the wanted board is standing and open (the
 * whole damage list splits a tranche), an assassination is deadlined and
 * pays only its designated assassin.
 */
class TargetKillPredicate : public HookSettledPredicate
{

private:

  /** Initialises the type's payload message with the tranche pool.  */
  virtual void InitPayload (Job& job, unsigned quota, Amount tranche)
      const = 0;

public:

  bool
  SettlesOnTargetKill () const override
  {
    return true;
  }

  Faction
  AudienceFaction (const Account& poster) const override
  {
    /* Visible to everyone: who can collect is constrained by the TARGET's
       faction (only its enemies can land damage or be hired), never the
       poster's.  */
    return Faction::INVALID;
  }

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"name", "n"};
    return keys;
  }

  Amount
  MinReward (const JobContext& jc) const override
  {
    /* A kill contract occupies one of the target's capped listing slots, so
       it must lock real value: the runtime-tunable bounty floor.  */
    return ParamsTable (jc.db)
        .Get ("min-bounty-reward",
              jc.ctx.RoConfig ()->params ().min_bounty_reward ());
  }

  bool ValidatePost (const JobContext& jc, const Account& poster,
                     const Json::Value& terms) const override;
  void ApplyPost (const JobContext& jc, Account& poster,
                  const Json::Value& terms, Job& job) const override;

};

bool
TargetKillPredicate::ValidatePost (const JobContext& jc,
                                   const Account& poster,
                                   const Json::Value& terms) const
{
  if (!terms["name"].isString ())
    return false;
  const std::string target = terms["name"].asString ();
  if (target == poster.GetName ())
    {
      LOG (WARNING)
          << poster.GetName () << " cannot post a kill contract on itself";
      return false;
    }
  {
    const auto t = jc.accounts.GetByName (target);
    if (t == nullptr || !t->IsInitialised ())
      {
        LOG (WARNING) << "Kill-contract target does not exist: " << target;
        return false;
      }
  }

  if (!terms["n"].isUInt () || !xaya::IsIntegerValue (terms["n"]))
    return false;
  const unsigned quota = terms["n"].asUInt ();
  const unsigned maxQuota = jc.ctx.RoConfig ()->params ().max_bounty_quota ();
  if (quota < 1 || quota > maxQuota)
    {
      LOG (WARNING) << "Kill quota out of range: " << quota;
      return false;
    }

  if (!RequireZeroCollateral (terms, "A kill contract"))
    return false;

  Amount reward;
  CHECK (CoinAmountFromJson (terms["r"], reward));
  if (reward / quota < 1)
    {
      LOG (WARNING)
          << "Reward " << reward << " is too small for quota " << quota;
      return false;
    }

  return true;
}

void
TargetKillPredicate::ApplyPost (const JobContext& jc, Account& poster,
                                const Json::Value& terms, Job& job) const
{
  job.SetLinkedName (terms["name"].asString ());

  Amount reward;
  CHECK (CoinAmountFromJson (terms["r"], reward));
  const unsigned quota = terms["n"].asUInt ();
  InitPayload (job, quota, reward / quota);
}

/**
 * Wanted-bounty: the standing, open-claim kill contract.  Each qualifying
 * kill pays one tranche, split equally across the distinct accounts on the
 * victim's damage list; the pool completes when drained.  No accept step, no
 * collateral, no fulfil op -- settlement happens entirely at the kill hook,
 * and the only exit is the notice-based cancel.
 */
class WantedPredicate : public TargetKillPredicate
{

private:

  void
  InitPayload (Job& job, const unsigned quota, const Amount tranche)
      const override
  {
    auto* wp = job.MutableProto ().mutable_wanted ();
    wp->set_quota (quota);
    wp->set_remaining (quota);
    wp->set_tranche (tranche);
  }

public:

  bool
  IsStanding () const override
  {
    return true;
  }

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

  JobOutcome OnTargetKill (const JobContext& jc, Job& job,
                           const std::set<std::string>& killOwners,
                           Amount& sharePerOwner) const override;

};

JobOutcome
WantedPredicate::OnTargetKill (const JobContext& jc, Job& job,
                               const std::set<std::string>& killOwners,
                               Amount& sharePerOwner) const
{
  CHECK (!killOwners.empty ());
  auto* wp = job.MutableProto ().mutable_wanted ();
  CHECK_GT (wp->remaining (), 0);

  const Amount tranche = wp->tranche ();
  CHECK_GE (job.GetReward (), tranche);

  /* One tranche leaves the escrow: split equally across the distinct killer
     accounts, with any division remainder burned (never redistributed).
     When the tranche cannot give every distinct killer at least one coin,
     nobody is paid -- crediting a zero-value completion would inflate the
     reputation counters for free.  The tranche is still consumed (burned as
     the remainder, which is never redistributed).  The caller accumulates
     the share across all pools and pays each owner once (PayKillShares).  */
  sharePerOwner = tranche / killOwners.size ();

  job.SetReward (job.GetReward () - tranche);
  wp->set_remaining (wp->remaining () - 1);

  LOG (INFO)
      << "Wanted board " << job.GetId () << " paid a tranche of " << tranche
      << " split across " << killOwners.size () << " hunter(s); "
      << wp->remaining () << " kill(s) remaining";

  if (wp->remaining () > 0)
    return JobOutcome::INVALID;

  /* Pool drained: whatever escrow is left is the division dust, burned by
     never crediting it anywhere.  */
  if (job.GetReward () > 0)
    LOG (INFO)
        << "Wanted board " << job.GetId () << " complete; burning dust "
        << job.GetReward ();
  return JobOutcome::DRAINED;
}

/**
 * Assassination: the designated hit.  The wanted pool's approval-required
 * counterpart: the poster names the assassin (assign -> accept), who alone
 * earns the tranches -- one slice per qualifying kill (the assassin on the
 * victim's damage list) while ACCEPTED.  The quota'th kill completes the
 * contract; at the deadline any earned slice makes it a pass (the unearned
 * remainder refunds the poster) while zero kills is a failure.  No
 * collateral: the poster risks only the fee, the assassin only reputation.
 */
class AssassinationPredicate : public TargetKillPredicate
{

private:

  void
  InitPayload (Job& job, const unsigned quota, const Amount tranche)
      const override
  {
    auto* ap = job.MutableProto ().mutable_assassination ();
    ap->set_quota (quota);
    ap->set_remaining (quota);
    ap->set_tranche (tranche);
  }

public:

  bool
  RequiresApproval () const override
  {
    return true;
  }

  bool ValidateAccept (const JobContext& jc, const Job& job,
                       const Account& worker) const override;
  JobOutcome OnExpire (const JobContext& jc, Job& job) const override;
  JobOutcome OnTargetKill (const JobContext& jc, Job& job,
                           const std::set<std::string>& killOwners,
                           Amount& sharePerOwner) const override;

};

bool
AssassinationPredicate::ValidateAccept (const JobContext& jc, const Job& job,
                                        const Account& worker) const
{
  /* The assassin must be an enemy of the TARGET: there is no friendly fire,
     so a same-faction assassin could never land a qualifying kill (and the
     target itself can never be hired for its own hit).  The name check comes
     first so the target's handle is never opened while it IS the live worker
     handle (UniqueHandles).  */
  const std::string& target = job.GetLinkedName ();
  if (worker.GetName () == target)
    {
      LOG (WARNING) << target << " cannot accept the hit on itself";
      return false;
    }
  const auto t = GetAccountChecked (jc, target);
  if (t->GetFaction () == worker.GetFaction ())
    {
      LOG (WARNING)
          << worker.GetName () << " is in the target's own faction for job "
          << job.GetId ();
      return false;
    }
  return true;
}

JobOutcome
AssassinationPredicate::OnTargetKill (const JobContext& jc, Job& job,
                                      const std::set<std::string>& killOwners,
                                      Amount& sharePerOwner) const
{
  /* Only the designated assassin's kills count, and only once hired: a
     listed-but-unaccepted hit pays nobody.  */
  if (job.GetStatus () != Job::Status::ACCEPTED
        || killOwners.count (job.GetWorker ()) == 0)
    return JobOutcome::INVALID;

  auto* ap = job.MutableProto ().mutable_assassination ();
  CHECK_GT (ap->remaining (), 0);
  const Amount tranche = ap->tranche ();
  CHECK_GE (job.GetReward (), tranche);

  job.SetReward (job.GetReward () - tranche);
  ap->set_remaining (ap->remaining () - 1);
  const bool complete = (ap->remaining () == 0);

  /* One slice goes to the assassin alone, immediately; `sharePerOwner`
     stays 0 (this pays a designated worker directly, not the damage list).
     The reputation credit waits for settlement, where the whole contract
     counts ONCE -- here on the completing kill, else at the expiry pass.  */
  {
    auto worker = GetAccountChecked (jc, job.GetWorker ());
    ReleaseJobCoins (*worker, tranche);
    if (complete)
      BumpJobStats (*worker, true, Amount (ap->quota ()) * tranche);
  }

  LOG (INFO)
      << "Assassination " << job.GetId () << " paid a slice of " << tranche
      << " to " << job.GetWorker () << "; " << ap->remaining ()
      << " kill(s) remaining";

  if (!complete)
    return JobOutcome::INVALID;

  /* Contract fulfilled: whatever escrow is left is the division dust,
     burned by never crediting it anywhere.  */
  if (job.GetReward () > 0)
    LOG (INFO)
        << "Assassination " << job.GetId () << " complete; burning dust "
        << job.GetReward ();
  return JobOutcome::COMPLETED;
}

JobOutcome
AssassinationPredicate::OnExpire (const JobContext& jc, Job& job) const
{
  if (job.GetStatus () != Job::Status::ACCEPTED)
    {
      VoidJobAtHook (jc, job);
      return JobOutcome::VOID;
    }

  /* Earned slices were already paid at the kills, so the reward column --
     the unearned remainder plus any division dust -- always refunds.  */
  {
    auto poster = GetAccountChecked (jc, job.GetPoster ());
    ReleaseJobCoins (*poster, job.GetReward ());
  }

  const auto& ap = job.GetProto ().assassination ();
  const unsigned done = ap.quota () - ap.remaining ();
  const Amount tranche = ap.tranche ();
  auto worker = GetAccountChecked (jc, job.GetWorker ());
  if (done > 0)
    {
      /* Any kill makes the expired contract a pass ("if you get one, it's
         a pass"), counted at the value actually earned.  */
      LOG (INFO)
          << "Assassination " << job.GetId () << " expired after " << done
          << " kill(s); pass for " << job.GetWorker ();
      BumpJobStats (*worker, true, Amount (done) * tranche);
      return JobOutcome::COMPLETED;
    }

  /* Zero kills: the assassin failed.  There is no collateral to forfeit,
     and no forfeit-as-poster mark either -- unlike the entity-fate types,
     the poster cannot force this outcome to harvest anything.  */
  LOG (INFO)
      << "Assassination " << job.GetId () << " expired with no kills; "
      << job.GetWorker () << " failed";
  BumpJobStats (*worker, false, 0);
  return JobOutcome::FAILED;
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
        << (destroyed ? " destroyed" : " alive at settlement")
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"b"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    static const std::vector<const char*> keys = {"b"};
    return keys;
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"b"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    static const std::vector<const char*> keys = {"b"};
    return keys;
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"ch"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    static const std::vector<const char*> keys = {"ch"};
    return keys;
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"ch", "to"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    /* The linked entity is the destination building; the protected
       character lives in the proto only.  */
    static const std::vector<const char*> keys = {"to"};
    return keys;
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"x", "y", "rad", "k", "sp"};
    return keys;
  }

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
 * the goods lost, consumed, destroyed or simply not at the handover
 * building when the expiry sweep looks (the sweep is the observation
 * point; see the policy note below) -- defaults the whole escrow to the
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
      /* The explicit cast negates in the signed domain (the proto count is
         unsigned, and count validation keeps it in Quantity range).  */
      inv->GetInventory ().AddFungibleCount (
          rp.item (), -static_cast<Quantity> (rp.count ()));
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
     fate.  Any loss is a non-return covered by the deposit -- war risk sits
     with the renter by design.

     The deadline test for "returned" is the SWEEP'S view of the handover
     inventory (explicit policy, pinned 2026-07-16): expiry runs on the next
     superblock after the deadline, and a renter who lands the goods in that
     gap still settles as a clean return.  The at-most-one-superblock grace
     is intended forgiveness, not a defect -- an explicit fulfil in the same
     gap is still rejected (JobIsDue); only the goods actually sitting at
     the handover building when the sweep looks can cure a default.  */

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"b", "i", "n", "rent", "w"};
    return keys;
  }

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
           jobs_completed counters on both sides for just the posting fee.
           Rent >= 1 only adds paid friction (between colluders the rent
           circulates; the burned fee is the real cost) -- which is all it
           is meant to do, since raw counts are display-only and the
           value-weighted counter is fee-backed rather than collusion-proof
           (see BumpJobStats).  */
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
 * first expiry sweep after the deadline (success-on-expiry); the entity hook
 * refunds it if the building dies first, and a sale of the building any time
 * before that sweep voids and refunds it too.  After accept, rendering is
 * unconditional -- the owner has no take-down lever for the paid period, and
 * clients render only hash-matching content.
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys
        = {"b", "slot", "hash", "start"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    static const std::vector<const char*> keys = {"b"};
    return keys;
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
       re-check anyway.

       Pinned simple rule: this also voids an ACCEPTED ad already past its
       deadline but not yet swept, refunding rent the sweep would have paid
       the (old) owner.  At arm's length only the owner can trigger that and
       the owner is the losing payee; a colluding advertiser/owner pair
       extracts nothing (their rent circulates inside the pair either way).
       A status/due special case here would buy nothing but complexity.  */
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

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"ch", "w"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    static const std::vector<const char*> keys = {"ch"};
    return keys;
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
const AssassinationPredicate ASSASSINATION_PREDICATE;

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
    {Job::Type::ASSASSINATION, "assassination", &ASSASSINATION_PREDICATE},
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

std::vector<Database::IdT>
JobPredicate::PostLinkedIds (const Json::Value& terms) const
{
  std::vector<Database::IdT> ids;
  for (const auto* key : PostLinkedIdKeys ())
    {
      Database::IdT id;
      CHECK (IdFromJson (terms[key], id));
      ids.push_back (id);
    }
  return ids;
}

void
PayKillShares (const JobContext& jc, const std::set<std::string>& owners,
               const unsigned payingPools, const Amount totalShare)
{
  CHECK_GT (payingPools, 0);
  CHECK_GT (totalShare, 0);
  for (const auto& owner : owners)
    {
      auto a = GetAccountChecked (jc, owner);
      ReleaseJobCoins (*a, totalShare);
      BumpJobStats (*a, true, totalShare, payingPools);
    }
}

const char*
JobTypeName (const Job::Type type)
{
  for (const auto& entry : JOB_TYPE_REGISTRY)
    if (type == entry.type)
      return entry.name;
  return nullptr;
}

} // namespace pxd
