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
 * an accept would resurrect a listing that should void (an elapsed ad would
 * otherwise pay its full rent for zero display
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

const char*
DealSettleModeName (const proto::DealPayload::SettleMode mode)
{
  switch (mode)
    {
    case proto::DealPayload::BOTH_CONFIRM:
      return "both-confirm";
    case proto::DealPayload::RULING:
      return "ruling";
    case proto::DealPayload::SINGLE_CONFIRM:
      return "single-confirm";
    case proto::DealPayload::GHOST_SPLIT:
      return "ghost-split";
    case proto::DealPayload::REFUND:
      return "refund";
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
 * POST applies to every entity it would link.  One indexed equality count
 * plus one point parameter read, on move processing only.
 */
bool
EntityAtLinkedCap (const JobContext& jc, const Database::IdT id)
{
  return jc.jobs.CountForLinkedId (id)
      >= jc.params.Get (
            "max-jobs-per-linked-entity",
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
                 const int64_t d, const Amount rew,
                 const Amount col, const Json::Value& tm)
    : JobOperation(a, c), type(t), deadlineSecs(d),
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
  /* A deadlined listing may sit on the board up to the booking horizon (for
     ads this is how far ahead a calendar window can be rented); the deadline's
     own lower bound is left to each type (ads carry a window-length check).  */
  const auto& p = jc.ctx.RoConfig ()->params ();
  if (!standing && deadlineSecs > p.max_listing_window ())
    {
      LOG (WARNING) << "Job listing window too long: " << deadlineSecs;
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
     linked IDs / names) are known-valid.  */
  {
    /* Minimum escrowed value: the global floor, raised further by the
       type (wanted pools), so the capped board -- and especially a
       target's pool slots -- cannot be occupied for pocket change.  */
    const Amount minReward
        = std::max<Amount> (jc.params.Get ("min-job-reward",
                                           p.min_job_reward ()),
                            pred->MinReward (jc));
    if (reward < minReward)
      {
        LOG (WARNING)
            << "Reward " << reward << " is below the minimum " << minReward;
        return false;
      }

    if (jc.jobs.CountAll ()
          >= jc.params.Get ("max-live-jobs", p.max_live_jobs ()))
      {
        LOG (WARNING) << "Jobs board is at the live-jobs cap";
        return false;
      }

    if (jc.jobs.CountForPoster (account.GetName ())
          >= jc.params.Get ("max-jobs-per-poster", p.max_jobs_per_poster ()))
      {
        LOG (WARNING)
            << account.GetName () << " is at their live-jobs cap";
        return false;
      }

    for (const auto id : pred->PostLinkedIds (terms))
      if (EntityAtLinkedCap (jc, id))
        {
          LOG (WARNING) << "Entity " << id << " is at its linked-jobs cap";
          return false;
        }

    /* The stacked-listings cap bounds how many pools may target one name,
       so a target cannot be buried under more listings than the cap
       allows.  The target name is derived through the predicate (like the
       linked IDs above), never read out of a type's private grammar.  */
    const std::string target = pred->PostLinkedName (terms);
    if (!target.empty ()
          && jc.jobs.CountForLinkedName (target)
               >= jc.params.Get ("max-bounty-pools-per-target",
                                 p.max_bounty_pools_per_target ()))
      {
        LOG (WARNING)
            << "Target " << target << " is at the stacked-listings cap";
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
  /* Assignment designates an exclusive worker before anyone accepts: on the
     one assignable type -- the generic deal -- this is the private /
     invite-only deal, where only the designated worker may accept (the
     generic accept gate enforces the pin).  A standing job (the wanted board)
     is never accepted by a single worker, so a designated_worker on it would
     be dead data polluting the public JSON; the type decides, not the
     deadline column -- a notice-cancelled standing job carries a deadline but
     is still standing.  An approval type (the ad slot) manages its own
     designation and is rejected just below.  */
  const auto* pred = PredicateForType (job->GetType ());
  CHECK (pred != nullptr);
  if (pred->IsStanding ())
    {
      LOG (WARNING)
          << "Job " << jobId << " is standing; cannot designate a worker";
      return false;
    }
  /* An approval type's designation is type-managed, not the poster's to
     change: the ad-slot pins it at post to the building owner, whose accept
     is the content approval.  Re-designating could only strand the listing
     (nobody could ever satisfy both the designation and the ownership
     check), so it is rejected like any other invalid assign.  */
  if (pred->RequiresApproval ())
    {
      LOG (WARNING)
          << "Job " << jobId << " manages its own designation; cannot assign";
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
 * can add type-specific checks (ValidateAccept).
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

  /* Type-specific accept checks (the ad-slot owner-approval / exclusivity).  */
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

  ReleaseJobCoins (account, reward);
  SettleAndDelete (jc.jobs, std::move (job), JobOutcome::CANCELLED, jc.ctx);
}

/* ************************************************************************** */

/**
 * DEAL: the escrow-deal-specific actions beyond the generic post/accept --
 * confirm (happy path), dispute, and the bound arbiter's ruling.  One "dl"
 * object per op, exactly two members:
 *   {"dl":<id>,"confirm":true}   either party marks the deal done
 *   {"dl":<id>,"dispute":true}   either party contests it
 *   {"dl":<id>,"rule":<p>}       the bound arbiter rules p in {0,10,...,100}
 */
class DealOperation : public JobOperation
{

public:

  enum class Kind { CONFIRM, DISPUTE, RULE };

private:

  const Database::IdT id;
  const Kind kind;
  /** The ruling percentage, only for RULE (range-checked at parse).  */
  const int rulP;

public:

  DealOperation (Account& a, const JobContext& c, const Database::IdT i,
                 const Kind k, const int p)
    : JobOperation(a, c), id(i), kind(k), rulP(p)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
DealOperation::IsValid () const
{
  auto job = jc.jobs.GetById (id);
  if (job == nullptr || job->GetType () != Job::Type::DEAL)
    return false;
  /* Every deal action needs the escrow locked (both stakes in).  */
  if (job->GetStatus () != Job::Status::ACCEPTED)
    {
      LOG (WARNING) << "Deal op on a non-accepted deal " << id;
      return false;
    }

  /* An elapsed deal is the sweep's to settle (see JobIsDue), exactly like
     every other lifecycle op: a confirm, dispute or ruling landing between
     the end date and the next superblock would still change the settlement
     of a deal whose outcome is already fixed by the state at its deadline.  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Deal " << id << " is at or past its end date; only the sweep"
          << " settles it now";
      return false;
    }

  const auto& d = job->GetProto ().deal ();
  const std::string me = account.GetName ();
  const bool isParty = (me == job->GetPoster () || me == job->GetWorker ());

  switch (kind)
    {
    case Kind::CONFIRM:
      if (!isParty)
        return false;
      /* Once disputed, only the arbiter's ruling settles the deal (or the
         sweep's 50/50 fallback) -- the documented DealPayload invariant.
         A both-confirm racing the ruling would bypass the arbiter.  */
      if (d.disputed ())
        return false;
      /* No double-confirm.  */
      if (me == job->GetPoster () && d.poster_confirmed ())
        return false;
      if (me == job->GetWorker () && d.worker_confirmed ())
        return false;
      return true;

    case Kind::DISPUTE:
      if (!isParty)
        return false;
      if (d.disputed ())
        return false;
      /* A confirmation is irrevocable and waives only the confirmer's OWN
         dispute right (design §6.2): the counterparty may still dispute a
         shoddy job.  Without this guard a confirmed all-clear could be
         revoked -- downgrading the one-confirm p=100 timeout into the
         disputed 50/50 fallback, or (with a poster-owned arbiter) reopening
         a ruling path after the worker relied on the confirmation.  */
      if (me == job->GetPoster () && d.poster_confirmed ())
        return false;
      if (me == job->GetWorker () && d.worker_confirmed ())
        return false;
      return true;

    case Kind::RULE:
      /* Only the bound arbiter may rule, and only a raised dispute.  */
      if (d.arbiter ().empty () || me != d.arbiter ())
        return false;
      if (!d.disputed ())
        return false;
      return true;
    }

  return false;
}

void
DealOperation::Execute ()
{
  auto job = jc.jobs.GetById (id);
  CHECK (job != nullptr) << "Deal disappeared: " << id;
  auto& d = *job->MutableProto ().mutable_deal ();

  switch (kind)
    {
    case Kind::CONFIRM:
      if (account.GetName () == job->GetPoster ())
        d.set_poster_confirmed (true);
      else
        d.set_worker_confirmed (true);
      if (d.poster_confirmed () && d.worker_confirmed ())
        {
          /* Both sides agree it is done: release at p=100.  */
          const JobOutcome oc = SettleDeal (jc, *job, 100, &account, true,
                                            proto::DealPayload::BOTH_CONFIRM);
          SettleAndDelete (jc.jobs, std::move (job), oc, jc.ctx);
        }
      /* Otherwise the single confirm persists when the handle destructs.  */
      return;

    case Kind::DISPUTE:
      d.set_disputed (true);
      return;

    case Kind::RULE:
      {
        const JobOutcome oc = SettleDeal (jc, *job, rulP, &account, true,
                                          proto::DealPayload::RULING);
        SettleAndDelete (jc.jobs, std::move (job), oc, jc.ctx);
      }
      return;
    }
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
  const bool hasDl = data.isMember ("dl");
  if (hasT + hasS + hasA + hasC + hasDl != 1)
    return nullptr;

  std::unique_ptr<JobOperation> op;

  if (hasT)
    {
      /* POST: {"t":<type>,"d":<secs>,"r":<reward>,"co":<collateral>,...}.  The
         listing deadline "d" is required except for the standing types
         (wanted), which must omit it -- checked against the type in IsValid.  */
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
      static const std::set<std::string> generic = {"t", "d", "r", "co"};
      const auto& typeKeys = pred->PostTermKeys ();
      for (const auto& member : data.getMemberNames ())
        if (generic.count (member) == 0
              && std::find (typeKeys.begin (), typeKeys.end (), member)
                   == typeKeys.end ())
          {
            LOG (WARNING) << "Unknown key \"" << member << "\" in job post";
            return nullptr;
          }

      int64_t deadlineSecs = -1;
      if (data.isMember ("d"))
        {
          if (!data["d"].isInt64 () || !xaya::IsIntegerValue (data["d"]))
            return nullptr;
          deadlineSecs = data["d"].asInt64 ();
          if (deadlineSecs < 0)
            return nullptr;
        }
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
      /* DEAL action: {"dl":<id>, <one of confirm/dispute/rule>} -- exactly two
         members, exactly one action.  */
      CHECK (hasDl);
      if (data.size () != 2)
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["dl"], id))
        return nullptr;
      const bool hasConfirm = data.isMember ("confirm");
      const bool hasDispute = data.isMember ("dispute");
      const bool hasRule = data.isMember ("rule");
      if (hasConfirm + hasDispute + hasRule != 1)
        return nullptr;
      if (hasConfirm)
        {
          if (!data["confirm"].isBool () || !data["confirm"].asBool ())
            return nullptr;
          op = std::make_unique<DealOperation> (
              acc, jc, id, DealOperation::Kind::CONFIRM, 0);
        }
      else if (hasDispute)
        {
          if (!data["dispute"].isBool () || !data["dispute"].asBool ())
            return nullptr;
          op = std::make_unique<DealOperation> (
              acc, jc, id, DealOperation::Kind::DISPUTE, 0);
        }
      else
        {
          if (!data["rule"].isInt64 () || !xaya::IsIntegerValue (data["rule"]))
            return nullptr;
          const int64_t p = data["rule"].asInt64 ();
          if (p < 0 || p > 100 || p % 10 != 0)
            return nullptr;
          op = std::make_unique<DealOperation> (
              acc, jc, id, DealOperation::Kind::RULE, static_cast<int> (p));
        }
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
  JobsTable jobs;
  ParamsTable params;

  explicit BlockHookTables (Database& d)
    : accounts(d), buildings(d), jobs(d), params(d)
  {}

  JobContext
  MakeContext (const pxd::Context& ctx)
  {
    return {ctx, accounts, buildings, jobs, params};
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
  Faction victimFaction = Faction::INVALID;
  {
    const auto victim = characters.GetById (target.id ());
    CHECK (victim != nullptr);
    victimOwner = victim->GetOwner ();
    victimFaction = victim->GetFaction ();
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

  /* Derive the distinct HOSTILE killer accounts.  Fame credits every attacker
     on the damage list, but a bounty must not: a same-faction attacker can
     only reach the victim's list via mentecon friendly-fire (the confusion
     effect that turns a unit on its own side) -- never a legitimate hunter.
     Dropping same-faction owners BEFORE the divisor and any pool
     mutation kills the target's self-pay, an ally's share-dilution, and the
     zero-share pool-destruction all at once; if no hostile owner remains, the
     empty-set short-circuit below pays nobody and consumes nothing.  Kills
     with an empty owner set (e.g. turret-only damage, which is not
     damage-listed) likewise pay and consume nothing.  */
  std::set<std::string> owners;
  for (const auto attackerId : dl.GetAttackers (target.id ()))
    {
      auto c = characters.GetById (attackerId);
      CHECK (c != nullptr);
      if (c->GetFaction () == victimFaction)
        continue;
      owners.insert (c->GetOwner ());
    }
  if (owners.empty ())
    return;

  /* Consume one tranche per pool on this name (stacked listings all pay).  A
     wanted pool touches no accounts and reports its per-owner share here;
     those shares accumulate across pools and every distinct killer is paid
     ONCE at the end (PayKillShares) -- so account work stays O(pools + owners),
     a large distinct-killer mob never multiplying account writes by the pool
     count.  Each pool returns its settlement outcome: INVALID keeps it on the
     board (a pool with tranches left), anything else is recorded and the row
     deleted.  */
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
            /* Every worker-setting path guarantees an initialised account
               (the move-processor's init gate, the assign check), so the
               validator pins the full invariant -- same as the poster's.  */
            const auto worker = accounts.GetByName (j->GetWorker ());
            CHECK (worker != nullptr && worker->IsInitialised ())
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
        case JobLinkedKind::NONE:
          CHECK_EQ (linked, Database::EMPTY_ID)
              << "Job " << id << " should not have a linked entity";
          break;
        }
    }
}

} // namespace pxd
