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

#include <xayautil/jsonutils.hpp>

#include <glog/logging.h>

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace pxd
{

namespace
{

/** Maximum length in bytes of the free-text terms of a job.  */
constexpr size_t MAX_TERMS_LENGTH = 1'000;

/** Maximum value of the tag of a job.  */
constexpr unsigned MAX_TAG = 100;

/**
 * Returns the given number of basis points of an amount.  The config does
 * not bound the basis points, so that the result saturates instead of
 * overflowing.
 */
Amount
BasisPoints (const Amount amount, const int64_t bps)
{
  if (bps <= 0)
    return 0;
  if (amount > std::numeric_limits<Amount>::max () / bps)
    return std::numeric_limits<Amount>::max ();
  return amount * bps / 10'000;
}

/**
 * Parses an unsigned integer of at most the given value from JSON.
 */
bool
UIntFromJson (const Json::Value& val, const unsigned max, unsigned& res)
{
  if (!val.isUInt () || !xaya::IsIntegerValue (val))
    return false;

  res = val.asUInt ();
  return res <= max;
}

/**
 * Returns true if the name is an initialised account that can take part
 * in jobs, other than the given one (for which the caller holds the handle).
 */
bool
IsOtherAccount (const JobContext& jc, const Account& self,
                const std::string& name)
{
  if (name.empty () || name == self.GetName ())
    return false;

  const auto a = jc.accounts.GetByName (name);
  return a != nullptr && a->IsInitialised ();
}

/**
 * Returns true if the job has reached its deadline.  It is then settled
 * with the next superblock, and moves can no longer change it.
 */
bool
IsDue (const Job& job, const Context& ctx)
{
  return job.GetDeadline () <= ctx.Timestamp ();
}

/**
 * Removes a job from the database.  The handle is released first, so that
 * it does not write the row again afterwards.
 */
void
Remove (JobsTable& jobs, JobsTable::Handle job)
{
  const auto id = job->GetId ();
  job.reset ();
  jobs.DeleteById (id);
}

/**
 * Runs the function on the account with the given name.  If that is the
 * account whose move is being processed (sender, which is null outside of
 * moves), its existing handle is used, as there can be only one per account.
 */
template <typename Fcn>
  void
  UpdateAccount (const JobContext& jc, Account* sender,
                 const std::string& name, const Fcn& fcn)
{
  if (sender != nullptr && sender->GetName () == name)
    {
      fcn (*sender);
      return;
    }

  auto a = jc.accounts.GetByName (name);
  CHECK (a != nullptr) << "Account does not exist: " << name;
  fcn (*a);
}

/**
 * Gives the reward of a job back to the poster and the collateral back
 * to the worker (if it has one).
 */
void
Refund (const JobContext& jc, const Job& job, Account* sender)
{
  UpdateAccount (jc, sender, job.GetPoster (), [&job] (Account& a)
    {
      a.AddBalance (job.GetReward ());
    });

  if (job.IsAccepted ())
    UpdateAccount (jc, sender, job.GetWorker (), [&job] (Account& a)
      {
        a.AddBalance (job.GetCollateral ());
      });
}

/* ************************************************************************** */

/**
 * Posts a new job, locking the reward and burning the posting fee:
 *
 *   {"t": "deal", "d": seconds, "r": reward, "co": collateral}
 *
 * Optional are "arbiter", its "fee" in basis points, "w" (the only account
 * that may accept; if it is the empty string, nobody may until the poster
 * assigns one), and "tag" and "terms", which are stored for the parties and
 * the arbiter but not used by the game.
 */
class PostOperation : public JobOperation
{

private:

  /** The time from now until the deadline in seconds.  */
  const int64_t duration;

  const Amount reward;
  const Amount collateral;

  /** The data for the new job as given in the move.  */
  const proto::JobData terms;

  /**
   * Returns the fee for posting the job.
   */
  Amount
  Fee () const
  {
    const auto& params = jc.ctx.RoConfig ()->params ();
    return std::max<Amount> (params.job_post_fee_min (),
                             BasisPoints (reward, params.job_post_fee_bps ()));
  }

public:

  PostOperation (Account& a, const JobContext& c, const int64_t d,
                 const Amount r, const Amount co, const proto::JobData& t)
    : JobOperation(a, c), duration(d), reward(r), collateral(co), terms(t)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
PostOperation::IsValid () const
{
  const auto& params = jc.ctx.RoConfig ()->params ();
  const auto& name = account.GetName ();

  /* The second bound keeps the deadline from overflowing.  It can be
     extended twice, each time by less than the duration.  */
  const int64_t maxDuration
      = std::min<int64_t> (params.max_listing_window (),
                           (std::numeric_limits<int64_t>::max ()
                              - jc.ctx.Timestamp ()) / 3);
  if (duration > maxDuration)
    {
      LOG (WARNING) << "Job duration is too long: " << duration;
      return false;
    }

  /* The reward must not be zero in any case, since a job without reward
     would raise the statistics of the worker for free.  */
  const Amount minReward = std::max<Amount> (1, params.min_job_reward ());
  if (reward < minReward)
    {
      LOG (WARNING)
          << "Reward " << reward << " is below the minimum " << minReward;
      return false;
    }

  if (collateral > BasisPoints (reward, params.deal_max_collateral_bps ())
        || collateral > params.deal_max_collateral ())
    {
      LOG (WARNING) << "Collateral " << collateral << " is too large";
      return false;
    }

  /* The arbiter must not be a party of the deal.  This can only ensure
     that it is another account, not another person; but the worker knows
     the arbiter when accepting the job.  */
  if (terms.has_arbiter () && !IsOtherAccount (jc, account, terms.arbiter ()))
    {
      LOG (WARNING) << "Invalid arbiter: " << terms.arbiter ();
      return false;
    }
  if (terms.has_designated_worker ()
        && (terms.designated_worker () == terms.arbiter ()
              || !IsOtherAccount (jc, account, terms.designated_worker ())))
    {
      LOG (WARNING) << "Invalid worker: " << terms.designated_worker ();
      return false;
    }

  /* The tax of the current config is stored with the job.  Together with
     the fee, it must leave something of the reward.  */
  const int64_t taxBps = params.deal_tax_bps ();
  if (terms.fee_bps () > params.deal_max_fee_bps ()
        || taxBps + terms.fee_bps () >= 10'000)
    {
      LOG (WARNING)
          << "Arbiter fee " << terms.fee_bps () << " is too high with tax "
          << taxBps;
      return false;
    }

  /* The fee may be saturated, so that it is not added to the reward.  */
  const Amount fee = Fee ();
  if (account.GetBalance () - reward < fee)
    {
      LOG (WARNING)
          << name << " cannot afford reward " << reward << " and fee " << fee;
      return false;
    }

  if (jc.jobs.CountForPoster (name) >= params.max_jobs_per_poster ()
        || jc.jobs.CountAll () >= params.max_live_jobs ())
    {
      LOG (WARNING) << "There are too many jobs for " << name << " to post";
      return false;
    }

  return true;
}

void
PostOperation::Execute ()
{
  const auto& params = jc.ctx.RoConfig ()->params ();
  const Amount fee = Fee ();

  LOG (INFO)
      << account.GetName () << " posts a job with reward " << reward
      << ", collateral " << collateral << " and fee " << fee;

  account.AddBalance (-reward - fee);
  auto job = jc.jobs.CreateNew (account.GetName (), reward, collateral,
                                jc.ctx.Timestamp () + duration);

  /* Tax and reaction window are stored with the job, so that a later
     change of the config does not affect it.  */
  auto& pb = job->MutableProto ();
  pb = terms;
  pb.set_tax_bps (params.deal_tax_bps ());
  pb.set_reaction_window (std::clamp<int64_t> (params.deal_reaction_window (),
                                               0, duration));
}

/**
 * Parses a post operation from JSON.
 */
std::unique_ptr<JobOperation>
ParsePost (Account& acc, const JobContext& jc, const Json::Value& data)
{
  static const std::set<std::string> allowedKeys
      = {"t", "d", "r", "co", "arbiter", "fee", "w", "tag", "terms"};
  for (const auto& key : data.getMemberNames ())
    if (allowedKeys.count (key) == 0)
      return nullptr;

  if (!data["t"].isString () || data["t"].asString () != "deal")
    return nullptr;

  const auto& d = data["d"];
  if (!d.isInt64 () || !xaya::IsIntegerValue (d) || d.asInt64 () < 0)
    return nullptr;

  Amount reward, collateral;
  if (!CoinAmountFromJson (data["r"], reward)
        || !CoinAmountFromJson (data["co"], collateral))
    return nullptr;

  proto::JobData terms;
  unsigned num;

  if (data.isMember ("arbiter"))
    {
      const auto& val = data["arbiter"];
      if (!val.isString () || val.asString ().empty ())
        return nullptr;
      terms.set_arbiter (val.asString ());
    }

  if (data.isMember ("fee"))
    {
      /* There is nobody to get a fee without an arbiter.  */
      if (!terms.has_arbiter ()
            || !UIntFromJson (data["fee"],
                              std::numeric_limits<unsigned>::max (), num))
        return nullptr;
      terms.set_fee_bps (num);
    }

  if (data.isMember ("w"))
    {
      const auto& val = data["w"];
      if (!val.isString ())
        return nullptr;
      terms.set_invite_only (true);
      if (!val.asString ().empty ())
        terms.set_designated_worker (val.asString ());
    }

  if (data.isMember ("tag"))
    {
      if (!UIntFromJson (data["tag"], MAX_TAG, num))
        return nullptr;
      terms.set_type_tag (num);
    }

  if (data.isMember ("terms"))
    {
      const auto& val = data["terms"];
      if (!val.isString () || val.asString ().size () > MAX_TERMS_LENGTH)
        return nullptr;
      terms.set_terms (val.asString ());
    }

  return std::make_unique<PostOperation> (acc, jc, d.asInt64 (),
                                          reward, collateral, terms);
}

/* ************************************************************************** */

/**
 * Lets the poster of a job that is not yet accepted name the only account
 * that may accept it:
 *
 *   {"s": job id, "w": account}
 */
class AssignOperation : public JobOperation
{

private:

  const Database::IdT id;
  const std::string designated;

public:

  AssignOperation (Account& a, const JobContext& c, const Database::IdT i,
                   const std::string& w)
    : JobOperation(a, c), id(i), designated(w)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
AssignOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (id);
  if (job == nullptr || job->IsAccepted () || IsDue (*job, jc.ctx)
        || job->GetPoster () != account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " cannot assign job " << id;
      return false;
    }

  if (designated == job->GetProto ().arbiter ()
        || !IsOtherAccount (jc, account, designated))
    {
      LOG (WARNING)
          << "Job " << id << " cannot be assigned to " << designated;
      return false;
    }

  return true;
}

void
AssignOperation::Execute ()
{
  LOG (INFO)
      << account.GetName () << " assigns job " << id << " to " << designated;

  auto job = jc.jobs.GetById (id);
  CHECK (job != nullptr);
  job->MutableProto ().set_designated_worker (designated);
}

/* ************************************************************************** */

/**
 * Accepts a job as the worker, locking the collateral:
 *
 *   {"a": job id}
 */
class AcceptOperation : public JobOperation
{

private:

  const Database::IdT id;

public:

  AcceptOperation (Account& a, const JobContext& c, const Database::IdT i)
    : JobOperation(a, c), id(i)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
AcceptOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (id);
  if (job == nullptr || job->IsAccepted () || IsDue (*job, jc.ctx))
    {
      LOG (WARNING) << "Job " << id << " cannot be accepted";
      return false;
    }

  const auto& name = account.GetName ();
  const auto& pb = job->GetProto ();

  /* The worker must be a third account.  In particular, an arbiter that
     is also the worker could rule on its own dispute.  */
  if (name == job->GetPoster () || name == pb.arbiter ())
    {
      LOG (WARNING) << name << " is the poster or arbiter of job " << id;
      return false;
    }

  const auto& designated = pb.designated_worker ();
  if (designated.empty () ? pb.invite_only () : designated != name)
    {
      LOG (WARNING) << "Job " << id << " is not open for " << name;
      return false;
    }

  if (account.GetBalance () < job->GetCollateral ())
    {
      LOG (WARNING)
          << name << " cannot afford the collateral of job " << id;
      return false;
    }

  return true;
}

void
AcceptOperation::Execute ()
{
  LOG (INFO) << account.GetName () << " accepts job " << id;

  auto job = jc.jobs.GetById (id);
  CHECK (job != nullptr);
  account.AddBalance (-job->GetCollateral ());
  job->SetWorker (account.GetName ());
}

/* ************************************************************************** */

/**
 * Lets the poster withdraw a job that is not yet accepted.  The reward
 * is refunded, but not the posting fee:
 *
 *   {"c": job id}
 */
class CancelOperation : public JobOperation
{

private:

  const Database::IdT id;

public:

  CancelOperation (Account& a, const JobContext& c, const Database::IdT i)
    : JobOperation(a, c), id(i)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
CancelOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (id);
  if (job == nullptr || job->IsAccepted () || IsDue (*job, jc.ctx)
        || job->GetPoster () != account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " cannot cancel job " << id;
      return false;
    }

  return true;
}

void
CancelOperation::Execute ()
{
  LOG (INFO) << account.GetName () << " cancels job " << id;

  auto job = jc.jobs.GetById (id);
  CHECK (job != nullptr);
  Refund (jc, *job, &account);
  Remove (jc.jobs, std::move (job));
}

} // anonymous namespace

std::unique_ptr<JobOperation>
JobOperation::Parse (Account& acc, const Json::Value& data,
                     const JobContext& jc)
{
  /* The empty string is a valid account name.  But in the data of a job,
     it stands for no worker or arbiter, so that the account with that
     name cannot take part in jobs.  */
  if (acc.GetName ().empty () || !data.isObject ())
    return nullptr;

  /* Each operation has one key that tells what it is, and the code for
     it verifies that there are no keys but its own.  */

  if (data.isMember ("t"))
    return ParsePost (acc, jc, data);

  Database::IdT id;

  if (data.isMember ("s"))
    {
      if (data.size () != 2 || !IdFromJson (data["s"], id)
            || !data["w"].isString ())
        return nullptr;
      return std::make_unique<AssignOperation> (acc, jc, id,
                                                data["w"].asString ());
    }

  if (data.size () != 1)
    return nullptr;
  if (IdFromJson (data["a"], id))
    return std::make_unique<AcceptOperation> (acc, jc, id);
  if (IdFromJson (data["c"], id))
    return std::make_unique<CancelOperation> (acc, jc, id);

  return nullptr;
}

/* ************************************************************************** */

void
ExpireJobs (Database& db, const Context& ctx)
{
  AccountsTable accounts(db);
  JobsTable jobs(db);
  const JobContext jc{ctx, accounts, jobs};

  /* The query has to be done before the jobs are settled and removed.  */
  std::vector<Database::IdT> due;
  {
    auto res = jobs.QueryForDeadline (ctx.Timestamp ());
    while (res.Step ())
      due.push_back (res.Get<JobResult::id> ());
  }

  for (const auto id : due)
    {
      auto job = jobs.GetById (id);
      CHECK (job != nullptr);
      Refund (jc, *job, nullptr);
      Remove (jobs, std::move (job));
    }
}

void
ValidateJobs (Database& db)
{
  AccountsTable accounts(db);
  JobsTable jobs(db);

  auto res = jobs.QueryAll ();
  while (res.Step ())
    {
      const auto job = jobs.GetFromResult (res);

      std::vector<std::string> names = {job->GetPoster ()};
      if (job->IsAccepted ())
        names.push_back (job->GetWorker ());

      for (const auto& name : names)
        {
          const auto a = accounts.GetByName (name);
          CHECK (a != nullptr && a->IsInitialised ())
              << "Job " << job->GetId () << " has invalid account " << name;
        }
    }
}

} // namespace pxd
