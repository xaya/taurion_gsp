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

#include <glog/logging.h>

#include <algorithm>
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
/* Transport predicate.                                                       */

namespace
{

/**
 * The delivery family predicate: goods are procured (from anywhere) and must
 * end up at the destination building B (= the job's linked_id).  Fulfil comes
 * in two variants -- straight from a docked character's cargo, or from the
 * worker's own building inventory at B (multi-trip staging).  A foundation
 * destination delivers into the construction inventory (construction-supply).
 */
class TransportPredicate : public JobPredicate
{

private:

  /**
   * Parses and validates the item manifest out of a POST's terms.  Returns
   * false if malformed, empty, over the entry bound, or referencing an
   * unknown item type.
   */
  static bool ParseManifest (const JobContext& jc, const Json::Value& terms,
                             std::map<std::string, Quantity>& out);

public:

  bool ValidatePost (const JobContext& jc, const Account& poster,
                     const Json::Value& terms) const override;
  void ApplyPost (const JobContext& jc, const Account& poster,
                  const Json::Value& terms, Job& job) const override;
  bool CanFulfil (const JobContext& jc, const Job& job, const Account& worker,
                  const Json::Value& args) const override;
  FulfilResult DoFulfil (const JobContext& jc, Job& job, Account& worker,
                         const Json::Value& args) const override;
  void OnExpire (const JobContext& jc, Job& job) const override;
  void OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const override;

};

bool
TransportPredicate::ParseManifest (const JobContext& jc,
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
TransportPredicate::ApplyPost (const JobContext& jc, const Account& poster,
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

bool
TransportPredicate::CanFulfil (const JobContext& jc, const Job& job,
                               const Account& worker,
                               const Json::Value& args) const
{
  const auto destB = job.GetLinkedId ();
  const auto& manifest = job.GetProto ().transport ().manifest ().fungible ();

  if (args.isMember ("ch"))
    {
      Database::IdT chId;
      if (!IdFromJson (args["ch"], chId))
        return false;
      const auto ch = jc.characters.GetById (chId);
      if (ch == nullptr || ch->GetOwner () != worker.GetName ())
        {
          LOG (WARNING)
              << "Character " << chId << " missing or not owned by "
              << worker.GetName ();
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
      for (const auto& entry : manifest)
        if (inv.GetFungibleCount (entry.first)
              < static_cast<Quantity> (entry.second))
          return false;
      return true;
    }

  /* Own-inventory (multi-trip) variant: the worker has staged the goods into
     their own building inventory at B.  A foundation has no per-account
     inventory, so only the character-cargo variant works there.  */
  const auto b = jc.buildings.GetById (destB);
  if (b == nullptr || b->GetProto ().foundation ())
    return false;
  const auto src = jc.buildingInv.Get (destB, worker.GetName ());
  const auto& inv = src->GetInventory ();
  for (const auto& entry : manifest)
    if (inv.GetFungibleCount (entry.first)
          < static_cast<Quantity> (entry.second))
      return false;
  return true;
}

FulfilResult
TransportPredicate::DoFulfil (const JobContext& jc, Job& job, Account& worker,
                             const Json::Value& args) const
{
  const auto destB = job.GetLinkedId ();

  /* Snapshot the manifest so we can release each inventory handle before
     acquiring the next (item counts are conserved item-by-item).  */
  std::map<std::string, Quantity> manifest;
  for (const auto& entry : job.GetProto ().transport ().manifest ().fungible ())
    manifest.emplace (entry.first, entry.second);

  LOG (INFO)
      << worker.GetName () << " fulfilling transport job " << job.GetId ()
      << " to building " << destB;

  /* 1) Take the goods out of the source.  */
  if (args.isMember ("ch"))
    {
      Database::IdT chId;
      CHECK (IdFromJson (args["ch"], chId));
      auto ch = jc.characters.GetById (chId);
      CHECK (ch != nullptr);
      for (const auto& entry : manifest)
        ch->GetInventory ().AddFungibleCount (entry.first, -entry.second);
    }
  else
    {
      auto src = jc.buildingInv.Get (destB, worker.GetName ());
      for (const auto& entry : manifest)
        src->GetInventory ().AddFungibleCount (entry.first, -entry.second);
    }

  /* 2) Deposit into the destination.  */
  {
    auto b = jc.buildings.GetById (destB);
    CHECK (b != nullptr);
    if (b->GetProto ().foundation ())
      {
        Inventory inv (*b->MutableProto ().mutable_construction_inventory ());
        for (const auto& entry : manifest)
          inv.AddFungibleCount (entry.first, entry.second);
        /* Delivering may complete the material requirement and start the
           actual construction.  */
        MaybeStartBuildingConstruction (*b, jc.ongoings, jc.ctx);
      }
    else
      {
        auto tgt = jc.buildingInv.Get (destB, job.GetPoster ());
        for (const auto& entry : manifest)
          tgt->GetInventory ().AddFungibleCount (entry.first, entry.second);
      }
  }

  /* 3) Pay the worker the reward and return their collateral (fee-free).  */
  ReleaseJobCoins (worker, job.GetReward () + job.GetCollateral ());
  return FulfilResult::COMPLETE;
}

void
TransportPredicate::OnExpire (const JobContext& jc, Job& job) const
{
  auto poster = jc.accounts.GetByName (job.GetPoster ());
  CHECK (poster != nullptr) << "Poster account missing: " << job.GetPoster ();

  if (job.GetStatus () == Job::Status::ACCEPTED)
    {
      /* The worker failed to deliver in time: the reward refunds and the
         collateral forfeits, both to the poster (uniform forfeit-to-poster).  */
      LOG (INFO)
          << "Transport job " << job.GetId () << " expired unfulfilled; "
          << job.GetReward () << " + forfeited " << job.GetCollateral ()
          << " to poster " << job.GetPoster ();
      ReleaseJobCoins (*poster, job.GetReward () + job.GetCollateral ());
    }
  else
    {
      LOG (INFO)
          << "Open transport job " << job.GetId () << " expired; refunding "
          << job.GetReward () << " to poster " << job.GetPoster ();
      ReleaseJobCoins (*poster, job.GetReward ());
    }
}

void
TransportPredicate::OnLinkedEntityDestroyed (const JobContext& jc,
                                             Job& job) const
{
  /* The destination building died -- not the worker's fault.  Refund the
     reward to the poster and any locked collateral back to the worker.  */
  {
    auto poster = jc.accounts.GetByName (job.GetPoster ());
    CHECK (poster != nullptr) << "Poster account missing: " << job.GetPoster ();
    ReleaseJobCoins (*poster, job.GetReward ());
  }

  if (job.GetStatus () == Job::Status::ACCEPTED)
    {
      auto worker = jc.accounts.GetByName (job.GetWorker ());
      CHECK (worker != nullptr)
          << "Worker account missing: " << job.GetWorker ();
      ReleaseJobCoins (*worker, job.GetCollateral ());
    }

  LOG (INFO)
      << "Voided transport job " << job.GetId ()
      << ": destination building " << job.GetLinkedId () << " destroyed";
}

/** The process-lifetime transport predicate singleton.  */
const TransportPredicate TRANSPORT_PREDICATE;

} // anonymous namespace

Job::Type
JobTypeFromString (const std::string& name)
{
  if (name == "transport")
    return Job::Type::TRANSPORT;
  return Job::Type::INVALID;
}

const JobPredicate*
PredicateForType (const Job::Type type)
{
  switch (type)
    {
    case Job::Type::TRANSPORT:
      return &TRANSPORT_PREDICATE;
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
  const int64_t deadlineSecs;
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
                 const int64_t d, const Amount rew, const Amount col,
                 const Json::Value& tm)
    : JobOperation(a, c), type(t), deadlineSecs(d), reward(rew),
      collateral(col), terms(tm)
  {}

  bool IsValid () const override;
  Json::Value ToPendingJson () const override;
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

  if (reward < 0 || collateral < 0)
    return false;

  const auto& p = jc.ctx.RoConfig ()->params ();
  if (deadlineSecs < p.min_job_duration ()
        || deadlineSecs > p.max_job_duration ())
    {
      LOG (WARNING) << "Job duration out of range: " << deadlineSecs;
      return false;
    }

  const auto* pred = PredicateForType (type);
  CHECK (pred != nullptr);
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

Json::Value
PostOperation::ToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["op"] = "post";
  res["reward"] = IntToJson (reward);
  res["collateral"] = IntToJson (collateral);
  return res;
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

  auto job = jc.jobs.CreateNew (type, account.GetFaction (), account.GetName (),
                                reward, collateral);
  job->SetDeadline (jc.ctx.Timestamp () + deadlineSecs);
  PredicateForType (type)->ApplyPost (jc, account, terms, *job);
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
  Json::Value ToPendingJson () const override;
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

Json::Value
AssignOperation::ToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["op"] = "assign";
  res["id"] = IntToJson (jobId);
  res["worker"] = designated;
  return res;
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
 * designated-worker and approval rules are enforced here.  Generic.
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
  Json::Value ToPendingJson () const override;
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

  /* Accept-runway guard: reject accepting a deadlined job with too little
     time left to plausibly deliver (kills the accept-at-the-deadline trap).  */
  if (job->HasDeadline ())
    {
      const int64_t runway = job->GetDeadline () - jc.ctx.Timestamp ();
      if (runway < jc.ctx.RoConfig ()->params ().min_accept_runway ())
        {
          LOG (WARNING)
              << "Job " << jobId << " has only " << runway
              << "s of runway left; too little to accept";
          return false;
        }
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

  if (account.GetBalance () < job->GetCollateral ())
    {
      LOG (WARNING)
          << account.GetName () << " cannot afford collateral "
          << job->GetCollateral () << " for job " << jobId;
      return false;
    }

  return true;
}

Json::Value
AcceptOperation::ToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["op"] = "accept";
  res["id"] = IntToJson (jobId);
  return res;
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
}

/* ************************************************************************** */

/**
 * CANCEL: the poster withdraws an OPEN job before anyone accepts, and the
 * reward is refunded (the posting fee is not).  Generic.
 *
 * (Standing jobs cancel by notice; that path arrives with the wanted-bounty
 * type -- no standing types exist yet, so every job here has a deadline.)
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
  Json::Value ToPendingJson () const override;
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
  return true;
}

Json::Value
CancelOperation::ToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["op"] = "cancel";
  res["id"] = IntToJson (jobId);
  return res;
}

void
CancelOperation::Execute ()
{
  auto job = jc.jobs.GetById (jobId);
  CHECK (job != nullptr) << "Job disappeared: " << jobId;
  const Amount reward = job->GetReward ();

  LOG (INFO)
      << account.GetName () << " cancelling job " << jobId
      << ", refunding reward " << reward;

  job.reset ();
  ReleaseJobCoins (account, reward);
  jc.jobs.DeleteById (jobId);
}

/* ************************************************************************** */

/**
 * FULFIL: the worker submits the type-specific fulfil op; the predicate
 * verifies and settles.  Generic dispatch.
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
  Json::Value ToPendingJson () const override;
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
  if (job->GetWorker () != account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " is not the worker of job " << jobId;
      return false;
    }

  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);
  return pred->CanFulfil (jc, *job, account, args);
}

Json::Value
FulfilOperation::ToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["op"] = "fulfil";
  res["id"] = IntToJson (jobId);
  return res;
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
      job.reset ();
      jc.jobs.DeleteById (jobId);
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
      /* POST: {"t":<type>,"d":<secs>,"r":<reward>,"co":<collateral>,...}.
         The deadline is required (no standing types exist yet).  */
      if (!data["t"].isString ())
        return nullptr;
      const Job::Type type = JobTypeFromString (data["t"].asString ());
      if (type == Job::Type::INVALID || PredicateForType (type) == nullptr)
        return nullptr;
      if (!data.isMember ("d") || !data["d"].isInt64 ())
        return nullptr;
      const int64_t deadlineSecs = data["d"].asInt64 ();
      Amount reward, collateral;
      if (!CoinAmountFromJson (data["r"], reward)
            || !CoinAmountFromJson (data["co"], collateral))
        return nullptr;
      op = std::make_unique<PostOperation> (acc, jc, type, deadlineSecs,
                                            reward, collateral, data);
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

  if (op != nullptr)
    op->rawMove = data;

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
  JobsTable jobs;

  explicit BlockHookTables (Database& db)
    : accounts(db), buildings(db), buildingInv(db), characters(db),
      ongoings(db), jobs(db)
  {}

  JobContext
  MakeContext (const pxd::Context& ctx)
  {
    return {ctx, accounts, buildings, buildingInv, characters, ongoings, jobs};
  }
};

} // anonymous namespace

void
ExpireJobs (Database& db, const Context& ctx)
{
  JobsTable jobs(db);

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
      pred->OnExpire (jc, *j);
      j.reset ();
      tables.jobs.DeleteById (id);
    }
}

void
OnBuildingDestroyed (Database& db, const Context& ctx,
                     const Database::IdT buildingId)
{
  JobsTable jobs(db);

  /* Snapshot the affected jobs (fully consuming the query) before mutating
     any rows or balances.  */
  std::vector<Database::IdT> affected;
  {
    auto res = jobs.QueryForLinkedId (buildingId);
    while (res.Step ())
      {
        auto j = jobs.GetFromResult (res);
        affected.push_back (j->GetId ());
      }
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
      pred->OnLinkedEntityDestroyed (jc, *j);
      j.reset ();
      tables.jobs.DeleteById (id);
    }
}

} // namespace pxd
