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

#include "testutils.hpp"

#include "database/dbtest.hpp"
#include "database/roconfig.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <utility>

namespace pxd
{
namespace
{

/** The block timestamp at which the tests start.  */
constexpr int64_t BASE_TS = 1'000'000;

/** The duration of the standard job.  */
constexpr int64_t DAY = 86'400;

/** The reaction window in the config of the tests.  */
constexpr int64_t WINDOW = 30;

/** The initial balance of each account.  */
constexpr Amount START = 1'000'000;

/* ************************************************************************** */

class JobsTests : public DBTestWithSchema
{

private:

  std::unique_ptr<JobOperation>
  Parse (Account& a, const std::string& data)
  {
    return JobOperation::Parse (a, ParseJson (data), {ctx, accounts, jobs});
  }

protected:

  AccountsTable accounts;
  JobsTable jobs;

  ContextForTesting ctx;

  JobsTests ()
    : accounts(db), jobs(db)
  {
    ctx.SetTimestamp (BASE_TS);

    for (const auto* name : {"poster", "worker", "arbiter", "other"})
      {
        auto a = accounts.CreateNew (name);
        a->SetFaction (Faction::RED);
        a->AddBalance (START);
      }

    SetParams ([] (auto& p)
      {
        p.set_min_job_reward (1);
        p.set_deal_reaction_window (WINDOW);
      });
  }

  /**
   * Changes the parameters of the config, as an admin update does.
   */
  template <typename Fcn>
    void
    SetParams (const Fcn& mod)
  {
    proto::ConfigData cfg = *ctx.RoConfig ();
    mod (*cfg.mutable_params ());
    RoConfigStorage tbl(db);
    tbl.Set (cfg);
    ctx.SetRoConfig (tbl.Load (ctx.Chain ()));
  }

  Amount
  Balance (const std::string& name)
  {
    return accounts.GetByName (name)->GetBalance ();
  }

  /**
   * Returns true if the operation is well-formed.
   */
  bool
  ParseOk (const std::string& data)
  {
    auto a = accounts.GetByName ("poster");
    return Parse (*a, data) != nullptr;
  }

  /**
   * Processes an operation as the move processor does.  Returns false if
   * it is malformed or invalid.
   */
  bool
  Process (const std::string& name, const std::string& data)
  {
    auto a = accounts.GetByName (name);
    auto op = Parse (*a, data);
    if (op == nullptr || !op->IsValid ())
      return false;
    op->Execute ();
    return true;
  }

  bool
  Accept (const std::string& name, const Database::IdT id)
  {
    return Process (name, R"({"a":)" + std::to_string (id) + "}");
  }

  bool
  Cancel (const std::string& name, const Database::IdT id)
  {
    return Process (name, R"({"c":)" + std::to_string (id) + "}");
  }

  bool
  Assign (const std::string& name, const Database::IdT id,
          const std::string& worker)
  {
    return Process (name, R"({"s":)" + std::to_string (id)
                            + R"(,"w":")" + worker + R"("})");
  }

  /** Returns the ID of the job posted last.  */
  Database::IdT
  LatestJobId ()
  {
    Database::IdT id = 0;
    auto res = jobs.QueryAll ();
    while (res.Step ())
      id = res.Get<JobResult::id> ();
    CHECK_GT (id, 0);
    return id;
  }

  /**
   * Posts the standard job:  reward 5000 (with a posting fee of 50), the
   * given collateral and the arbiter with a fee of 10% (or no arbiter if
   * it is empty).  The tax is 3%.
   */
  Database::IdT
  PostDeal (const std::string& arbiter = "arbiter", const Amount co = 5'000)
  {
    std::string data = R"({"t":"deal","d":86400,"r":5000,"co":)"
                          + std::to_string (co);
    if (!arbiter.empty ())
      data += R"(,"arbiter":")" + arbiter + R"(","fee":1000)";
    CHECK (Process ("poster", data + "}"));
    return LatestJobId ();
  }

  /** Posts the standard job and lets the worker accept it.  */
  Database::IdT
  PostAcceptDeal (const std::string& arbiter = "arbiter",
                  const Amount co = 5'000)
  {
    const auto id = PostDeal (arbiter, co);
    CHECK (Accept ("worker", id));
    return id;
  }

  /** Runs the sweep after the deadline of all jobs.  */
  void
  Expire ()
  {
    ctx.SetTimestamp (BASE_TS + 2 * DAY);
    ExpireJobs (db, ctx);
  }

  bool
  JobExists (const Database::IdT id)
  {
    return jobs.GetById (id) != nullptr;
  }

  int64_t
  Deadline (const Database::IdT id)
  {
    return jobs.GetById (id)->GetDeadline ();
  }

};

/* ************************************************************************** */

TEST_F (JobsTests, ExpiryOfOpenJob)
{
  const auto id = PostDeal ();
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), START - 50);
}

TEST_F (JobsTests, ExpiryWithoutConfirmation)
{
  /* If neither party does anything, both get their coins back.  */
  const auto id = PostAcceptDeal ();
  Expire ();
  EXPECT_FALSE (JobExists (id));

  EXPECT_EQ (Balance ("worker"), START);
  EXPECT_EQ (Balance ("poster"), START - 50);
  EXPECT_EQ (Balance ("arbiter"), START);
}

TEST_F (JobsTests, ZeroDurationJobExpiresAtOnce)
{
  ASSERT_TRUE (Process ("poster", R"({"t":"deal","d":0,"r":5000,"co":1000})"));
  const auto id = LatestJobId ();
  EXPECT_FALSE (Accept ("worker", id));

  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), START - 50);
}

/* ************************************************************************** */

TEST_F (JobsTests, CancelRefundsReward)
{
  const auto id = PostDeal ();
  ASSERT_TRUE (Cancel ("poster", id));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), START - 50);
}

TEST_F (JobsTests, OnlyPosterCancelsBeforeAccept)
{
  const auto id = PostDeal ();
  EXPECT_FALSE (Cancel ("worker", id));
  EXPECT_FALSE (Cancel ("arbiter", id));
  EXPECT_FALSE (Cancel ("poster", id + 1));

  ASSERT_TRUE (Accept ("worker", id));
  EXPECT_FALSE (Cancel ("poster", id));
  EXPECT_TRUE (JobExists (id));
}

TEST_F (JobsTests, WhoCanAccept)
{
  const auto id = PostDeal ();
  EXPECT_FALSE (Accept ("poster", id));
  EXPECT_FALSE (Accept ("arbiter", id));
  EXPECT_FALSE (Accept ("worker", id + 1));

  EXPECT_TRUE (Accept ("worker", id));
  EXPECT_FALSE (Accept ("other", id));
  EXPECT_EQ (jobs.GetById (id)->GetWorker (), "worker");
  EXPECT_EQ (Balance ("worker"), START - 5'000);
}

TEST_F (JobsTests, AcceptNeedsCollateral)
{
  const auto id = PostDeal ();
  accounts.GetByName ("worker")->AddBalance (4'999 - START);
  EXPECT_FALSE (Accept ("worker", id));

  accounts.GetByName ("worker")->AddBalance (1);
  EXPECT_TRUE (Accept ("worker", id));
  EXPECT_EQ (Balance ("worker"), 0);
}

TEST_F (JobsTests, WhoCanBeAssigned)
{
  const auto id = PostDeal ();
  EXPECT_FALSE (Assign ("other", id, "worker"));
  EXPECT_FALSE (Assign ("poster", id + 1, "worker"));
  EXPECT_FALSE (Assign ("poster", id, "nobody"));
  EXPECT_FALSE (Assign ("poster", id, ""));
  EXPECT_FALSE (Assign ("poster", id, "arbiter"));
  EXPECT_FALSE (Assign ("poster", id, "poster"));

  EXPECT_TRUE (Assign ("poster", id, "worker"));
  ASSERT_TRUE (Accept ("worker", id));
  EXPECT_FALSE (Assign ("poster", id, "other"));
}

TEST_F (JobsTests, EmptyAccountName)
{
  /* The empty string is a valid name on the chain, but that account
     cannot take part in jobs in any role.  */
  {
    auto a = accounts.CreateNew ("");
    a->SetFaction (Faction::RED);
    a->AddBalance (START);
  }

  EXPECT_FALSE (Process ("", R"({"t":"deal","d":86400,"r":5000,"co":0})"));
  EXPECT_FALSE (ParseOk (R"({"t":"deal","d":86400,"r":5000,"co":0,)"
                         R"("arbiter":""})"));

  for (const auto* arbiter : {"arbiter", ""})
    {
      const auto id = PostDeal (arbiter);
      EXPECT_FALSE (Assign ("poster", id, ""));
      EXPECT_FALSE (Accept ("", id));
      EXPECT_FALSE (Cancel ("", id));
      EXPECT_FALSE (jobs.GetById (id)->IsAccepted ());

      ASSERT_TRUE (Accept ("worker", id));
    }

  Expire ();
  EXPECT_EQ (Balance (""), START);
}

TEST_F (JobsTests, AssignedJobOnlyForWorker)
{
  const auto id = PostDeal ();
  ASSERT_TRUE (Assign ("poster", id, "worker"));
  EXPECT_EQ (jobs.GetById (id)->GetProto ().designated_worker (), "worker");
  EXPECT_FALSE (jobs.GetById (id)->GetProto ().invite_only ());

  EXPECT_FALSE (Accept ("other", id));
  EXPECT_TRUE (Accept ("worker", id));
}

TEST_F (JobsTests, PostForDesignatedWorker)
{
  ASSERT_TRUE (Process ("poster", R"({"t":"deal","d":86400,"r":5000,)"
                                  R"("co":5000,"w":"worker"})"));
  const auto id = LatestJobId ();
  EXPECT_FALSE (Accept ("other", id));
  EXPECT_TRUE (Accept ("worker", id));
}

TEST_F (JobsTests, InviteOnlyWithoutWorker)
{
  /* Nobody can accept until the poster assigns a worker.  */
  ASSERT_TRUE (Process ("poster", R"({"t":"deal","d":86400,"r":5000,)"
                                  R"("co":5000,"w":""})"));
  const auto id = LatestJobId ();
  EXPECT_FALSE (Accept ("worker", id));

  ASSERT_TRUE (Assign ("poster", id, "worker"));
  EXPECT_FALSE (Accept ("other", id));
  EXPECT_TRUE (Accept ("worker", id));
}

/* ************************************************************************** */

TEST_F (JobsTests, InvalidFormat)
{
  const std::string tests[] =
    {
      "42",
      "[]",
      R"({"a":1,"c":1})",
      R"({"s":1,"w":"worker","c":1})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"a":1})",

      R"({"a":1,"x":0})",
      R"({"c":1,"x":0})",
      R"({"s":1,"w":"worker","x":0})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"x":0})",

      R"({"a":"1"})",
      R"({"a":0})",
      R"({"c":"1"})",
      R"({"s":"1","w":"worker"})",

      R"({"s":1})",
      R"({"s":1,"w":5})",

      R"({"t":"other","d":100,"r":5000,"co":0})",
      R"({"t":1,"d":100,"r":5000,"co":0})",
      R"({"t":"deal","r":5000,"co":0})",
      R"({"t":"deal","d":100,"co":0})",
      R"({"t":"deal","d":100,"r":5000})",
      R"({"t":"deal","d":-1,"r":5000,"co":0})",
      R"({"t":"deal","d":100,"r":-1,"co":0})",
      R"({"t":"deal","d":100,"r":5000,"co":-1})",

      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":""})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":5})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"fee":100})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":"arbiter",)"
        R"("fee":-1})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"w":5})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"tag":101})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"terms":")"
        + std::string (1'001, 'x') + R"("})",
    };

  for (const auto& t : tests)
    EXPECT_FALSE (ParseOk (t)) << t;
}

TEST_F (JobsTests, ValidFormat)
{
  const std::string tests[] =
    {
      R"({"a":1})",
      R"({"c":1})",
      R"({"s":1,"w":"worker"})",
      R"({"t":"deal","d":0,"r":0,"co":0})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":"arbiter","fee":0,)"
        R"("w":"","tag":100,"terms":")"
        + std::string (1'000, 'x') + R"("})",
    };

  for (const auto& t : tests)
    EXPECT_TRUE (ParseOk (t)) << t;
}

TEST_F (JobsTests, InvalidPost)
{
  SetParams ([] (auto& p)
    {
      p.set_max_listing_window (DAY);
      p.set_deal_max_collateral (8'000);
    });

  const std::string invalid[] =
    {
      R"({"t":"deal","d":86401,"r":5000,"co":0})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":"arbiter",)"
        R"("fee":1001})",
      R"({"t":"deal","d":100,"r":5000,"co":8001})",
      R"({"t":"deal","d":100,"r":1000,"co":2001})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":"nobody"})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":"poster"})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"w":"nobody"})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"w":"poster"})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":"arbiter",)"
        R"("w":"arbiter"})",
    };
  for (const auto& t : invalid)
    EXPECT_FALSE (Process ("poster", t)) << t;
  EXPECT_EQ (jobs.CountAll (), 0);
  EXPECT_EQ (Balance ("poster"), START);

  const std::string valid[] =
    {
      R"({"t":"deal","d":86400,"r":5000,"co":0})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"arbiter":"arbiter",)"
        R"("fee":1000})",
      R"({"t":"deal","d":100,"r":5000,"co":8000})",
      R"({"t":"deal","d":100,"r":1000,"co":2000})",
    };
  for (const auto& t : valid)
    EXPECT_TRUE (Process ("poster", t)) << t;
}

TEST_F (JobsTests, PosterPaysRewardAndFee)
{
  /* The posting fee is 1% of the reward:  9901 for the first one, which is
     one more than the balance leaves, and 9900 for the second.  */
  EXPECT_FALSE (Process ("poster", R"({"t":"deal","d":100,"r":990100,)"
                                   R"("co":0})"));
  EXPECT_TRUE (Process ("poster", R"({"t":"deal","d":100,"r":990099,)"
                                  R"("co":0})"));
  EXPECT_EQ (Balance ("poster"), 1);
}

TEST_F (JobsTests, MinimumReward)
{
  /* The reward must not be zero even if the config allows it.  */
  SetParams ([] (auto& p) { p.set_min_job_reward (0); });
  EXPECT_FALSE (Process ("poster", R"({"t":"deal","d":100,"r":0,"co":0})"));
  EXPECT_TRUE (Process ("poster", R"({"t":"deal","d":100,"r":1,"co":0})"));

  SetParams ([] (auto& p) { p.set_min_job_reward (200); });
  EXPECT_FALSE (Process ("poster", R"({"t":"deal","d":100,"r":199,"co":0})"));
  EXPECT_TRUE (Process ("poster", R"({"t":"deal","d":100,"r":200,"co":0})"));
}

TEST_F (JobsTests, JobLimits)
{
  SetParams ([] (auto& p)
    {
      p.set_max_live_jobs (3);
      p.set_max_jobs_per_poster (2);
    });

  const std::string post = R"({"t":"deal","d":86400,"r":5000,"co":0})";
  EXPECT_TRUE (Process ("poster", post));
  EXPECT_TRUE (Process ("poster", post));
  EXPECT_FALSE (Process ("poster", post));
  EXPECT_TRUE (Process ("worker", post));
  EXPECT_FALSE (Process ("other", post));
  EXPECT_EQ (jobs.CountAll (), 3);
}

} // anonymous namespace
} // namespace pxd
