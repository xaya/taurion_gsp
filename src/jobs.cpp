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

#include "jsonutils.hpp"

#include "database/params.hpp"

#include <xayautil/jsonutils.hpp>

#include <glog/logging.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace pxd
{

/* ************************************************************************** */
/* Shared settlement helpers.                                                 */

namespace
{

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

} // anonymous namespace

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
 * Whether the given entity is at the per-entity admission cap: the gate a
 * POST applies to every entity it would link, and an ACCEPT re-applies to
 * the entity it relinks to (AcceptRelinkId).  One indexed equality count
 * plus one point parameter read, on move processing only.
 */
bool
EntityAtLinkedCap (const JobContext& jc, const ParamsTable& par,
                   const Database::IdT id)
{
  return jc.jobs.CountForLinkedId (id)
      >= par.Get ("max-jobs-per-linked-entity",
                  jc.ctx.RoConfig ()->params ().max_jobs_per_linked_entity ());
}

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

  /* Admission caps: the deterministic ceiling on every future atomic
     sweep, enforced at the door so settlement semantics never change for
     rows already admitted.  The effective values are the admin-tunable
     parameter overrides (the "param" command) falling back to the roconfig
     defaults; 0 -- or any negative value -- freezes the respective
     admission entirely.  Checked after ValidatePost so the terms (and any
     linked IDs) are known-valid.  */
  {
    const ParamsTable par(jc.db);

    /* Minimum escrowed value: the global floor, raised further by the
       type (wanted pools), so the capped board -- and especially a
       target's pool slots -- cannot be occupied for pocket change.  */
    const Amount minReward
        = std::max<Amount> (par.Get ("min-job-reward", p.min_job_reward ()),
                            pred->MinReward (jc));
    if (reward < minReward)
      {
        LOG (WARNING)
            << "Reward " << reward << " is below the minimum " << minReward;
        return false;
      }

    if (jc.jobs.CountAll ()
          >= par.Get ("max-live-jobs", p.max_live_jobs ()))
      {
        LOG (WARNING) << "Jobs board is at the live-jobs cap";
        return false;
      }

    if (jc.jobs.CountForPoster (account.GetName ())
          >= par.Get ("max-jobs-per-poster", p.max_jobs_per_poster ()))
      {
        LOG (WARNING)
            << account.GetName () << " is at their live-jobs cap";
        return false;
      }

    for (const auto id : pred->PostLinkedIds (terms))
      if (EntityAtLinkedCap (jc, par, id))
        {
          LOG (WARNING) << "Entity " << id << " is at its linked-jobs cap";
          return false;
        }

    /* The stacked-listings cap covers every kill contract on a name (wanted
       pools and assassination hits share the one budget), so a target cannot
       be buried under more listings than the cap allows however the slots
       are split between the two types.  */
    if (pred->SettlesOnTargetKill ()
          && jc.jobs.CountForLinkedName (terms["name"].asString ())
               >= par.Get ("max-bounty-pools-per-target",
                           p.max_bounty_pools_per_target ()))
      {
        LOG (WARNING)
            << "Target " << terms["name"].asString ()
            << " is at the stacked-listings cap";
        return false;
      }
  }

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
  /* An expired listing is the sweep's to void (see JobIsDue); a designation
     landing in the gap would be dead data on a job that never runs.  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Job " << jobId << " is at or past its deadline; cannot assign";
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

  /* A type whose accept RELINKS the row (haul: source -> destination) must
     pass the per-entity admission gate for the new entity now: the POST
     check counts only rows currently linked, so OPEN hauls -- which link
     their sources -- do not show up against the destination until this
     moment.  A full destination keeps the job OPEN (see AcceptRelinkId).  */
  const auto relink = pred->AcceptRelinkId (*job);
  if (relink != Database::EMPTY_ID
        && EntityAtLinkedCap (jc, ParamsTable (jc.db), relink))
    {
      LOG (WARNING)
          << "Job " << jobId << " cannot relink to entity " << relink
          << ": it is at its linked-jobs cap";
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

  /* An expired listing is the sweep's to void (see JobIsDue); a cancel in
     the gap would record CANCELLED history for a job that expired.  The
     refund is the same either way.  Standing jobs never reach here with a
     deadline (rejected above), so the notice-cancel path is unaffected.  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Job " << jobId << " is at or past its deadline; cannot cancel";
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
      const auto* pred = PredicateForType (type);
      if (type == Job::Type::INVALID || pred == nullptr)
        return nullptr;

      /* The POST grammar is exactly as strict as the lifecycle ops' below:
         beyond the generic keys, only the type's own term keys are allowed,
         so a typo'd or unknown member rejects the move instead of being
         silently ignored.  */
      static const std::set<std::string> generic = {"t", "d", "wd", "r", "co"};
      const auto& typeKeys = pred->PostTermKeys ();
      for (const auto& member : data.getMemberNames ())
        if (generic.count (member) == 0
              && std::find (typeKeys.begin (), typeKeys.end (), member)
                   == typeKeys.end ())
          {
            LOG (WARNING) << "Unknown key \"" << member << "\" in job post";
            return nullptr;
          }

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
  Database& db;

  AccountsTable accounts;
  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;
  CharacterTable characters;
  OngoingsTable ongoings;
  GroundLootTable groundLoot;
  JobsTable jobs;

  explicit BlockHookTables (Database& d)
    : db(d), accounts(d), buildings(d), buildingInv(d), characters(d),
      ongoings(d), groundLoot(d), jobs(d)
  {}

  JobContext
  MakeContext (const pxd::Context& ctx)
  {
    return {db, ctx, accounts, buildings, buildingInv, characters, ongoings,
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
  const auto& params = ctx.RoConfig ()->params ();
  if (params.jobs_history_retention () > 0)
    {
      /* The batch bound is what keeps one huge cohort ageing out from
         forcing an unbounded single-statement delete, so a configuration
         without it is a bug, not a mode.  */
      CHECK_GT (params.jobs_history_prune_batch (), 0)
          << "jobs_history_prune_batch must be positive";
      jobs.PruneHistory (ctx.Timestamp () - params.jobs_history_retention (),
                         params.jobs_history_prune_batch ());
    }

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
  anyBounties = JobsTable (db).HasActiveBountyNames ();
}

void
JobsBountyTracker::UpdateForKill (const proto::TargetId& target)
{
  if (target.type () != proto::TargetId::TYPE_CHARACTER)
    return;
  if (!anyBounties)
    return;

  /* The pre-removal pass: the victim's row (and the damage lists) are still
     live here.  */
  std::string victimOwner;
  {
    const auto victim = characters.GetById (target.id ());
    CHECK (victim != nullptr);
    victimOwner = victim->GetOwner ();
  }
  if (noBounty.count (victimOwner) > 0)
    return;

  /* The indexed linked-name probe IS the membership check: pools on this
     name come back directly, so a death costs one equality-indexed query
     at most (repeat deaths of a memoised bounty-free owner cost none; an
     owner with live pools is re-probed per death, since the pools drain as
     they pay).  An empty result -- never under bounty, or an earlier kill
     in this very block drained and deleted the last pool -- just memoises
     the owner and moves on (this must NOT be a CHECK, or a target losing
     more characters than the pool has tranches in one block would halt
     every node).  */
  std::vector<Database::IdT> pools;
  {
    JobsTable jobs(db);
    auto res = jobs.QueryForLinkedName (victimOwner);
    while (res.Step ())
      pools.push_back (jobs.GetFromResult (res)->GetId ());
  }
  if (pools.empty ())
    {
      noBounty.insert (victimOwner);
      return;
    }

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

  /* Consume one tranche per contract on this name (stacked listings all
     pay).  A wanted pool touches no accounts and reports its per-owner share
     here; those shares accumulate across pools and every distinct killer is
     paid ONCE at the end (PayKillShares) -- so account work stays
     O(pools + owners), a large distinct-killer mob never multiplying account
     writes by the pool count.  An assassination instead pays its one
     designated assassin inside OnTargetKill (a single account write, handle
     opened and released there) and leaves the share at 0.  Each contract
     returns its settlement outcome: INVALID keeps it on the board (a wanted
     pool with tranches left, an assassination this kill did not qualify
     for), anything else is recorded and the row deleted.  */
  BlockHookTables tables(db);
  const JobContext jc = tables.MakeContext (ctx);
  Amount totalShare = 0;
  unsigned payingPools = 0;
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
      Amount share = 0;
      const JobOutcome outcome = pred->OnTargetKill (jc, *j, owners, share);
      if (share > 0)
        {
          totalShare += share;
          ++payingPools;
        }
      if (outcome != JobOutcome::INVALID)
        SettleAndDelete (tables.jobs, std::move (j), outcome, ctx);
      /* Still live: the mutated contract flushes when the handle destructs.  */
    }

  if (payingPools > 0)
    PayKillShares (jc, owners, payingPools, totalShare);
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
