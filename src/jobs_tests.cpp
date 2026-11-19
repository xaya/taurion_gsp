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

  /** Statistics of an account as number and value.  */
  using Stat = std::pair<unsigned, Amount>;

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

  Stat
  DealStats (const std::string& name)
  {
    const auto a = accounts.GetByName (name);
    return {a->GetProto ().deals_completed (),
            a->GetProto ().deals_value_completed ()};
  }

  Stat
  PosterStats (const std::string& name)
  {
    const auto a = accounts.GetByName (name);
    return {a->GetProto ().deals_posted_completed (),
            a->GetProto ().deals_posted_value ()};
  }

  Stat
  ArbiterStats (const std::string& name)
  {
    const auto a = accounts.GetByName (name);
    return {a->GetProto ().arbiter_rulings (),
            a->GetProto ().arbiter_value_ruled ()};
  }

  unsigned
  Disputed (const std::string& name)
  {
    return accounts.GetByName (name)->GetProto ().deals_disputed ();
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

  bool
  Confirm (const std::string& name, const Database::IdT id)
  {
    return Process (name,
                    R"({"dl":)" + std::to_string (id) + R"(,"confirm":true})");
  }

  bool
  Dispute (const std::string& name, const Database::IdT id)
  {
    return Process (name,
                    R"({"dl":)" + std::to_string (id) + R"(,"dispute":true})");
  }

  bool
  Rule (const std::string& name, const Database::IdT id, const int p)
  {
    return Process (name, R"({"dl":)" + std::to_string (id)
                            + R"(,"rule":)" + std::to_string (p) + "}");
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

TEST_F (JobsTests, SettlementAddsUp)
{
  const Amount amounts[] = {0, 1, 7, 999, 50'000, 100'000'000'000};
  const std::pair<int, int> rates[] =
    {
      {0, 0}, {0, 500}, {0, 1'000}, {300, 0}, {300, 500}, {300, 1'000},
      {1'000, 0}, {1'000, 500}, {1'000, 1'000}, {9'000, 999}, {0, 9'999},
    };

  for (const Amount r : amounts)
    for (const Amount c : amounts)
      for (const auto& [tax, fee] : rates)
        for (int p = 0; p <= 100; p += 10)
          {
            const auto s = ComputeDealSettlement (r, c, p, tax, fee);
            EXPECT_EQ (s.worker + s.poster + s.arbiter + s.treasury, r + c);
            EXPECT_GE (s.worker, c * p / 100);
            EXPECT_GE (s.poster, 0);
          }
}

TEST_F (JobsTests, TaxAndFeeIndependentOfRuling)
{
  /* Tax and fee of a reward of 1010 are 30 and 101, whatever is ruled and
     whatever the collateral is.  The worker gets its percentage of the
     other 879 and the collateral, rounded down.  */
  for (const Amount c : {0, 2'020})
    for (int p = 0; p <= 100; p += 10)
      {
        const auto s = ComputeDealSettlement (1'010, c, p, 300, 1'000);
        EXPECT_EQ (s.treasury, 30);
        EXPECT_EQ (s.arbiter, 101);
        EXPECT_EQ (s.worker, (879 + c) * p / 100);
      }
}

TEST_F (JobsTests, BothConfirm)
{
  const auto id = PostAcceptDeal ();
  ASSERT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (JobExists (id));
  ASSERT_TRUE (Confirm ("worker", id));
  EXPECT_FALSE (JobExists (id));

  /* The worker gets its collateral back and the reward less tax and fee.  */
  EXPECT_EQ (Balance ("worker"), START + 5'000 - 150 - 500);
  EXPECT_EQ (Balance ("poster"), START - 5'000 - 50);
  EXPECT_EQ (Balance ("arbiter"), START + 500);

  EXPECT_EQ (DealStats ("worker"), Stat (1, 5'000));
  EXPECT_EQ (PosterStats ("poster"), Stat (1, 5'000));
  EXPECT_EQ (Disputed ("worker"), 0u);
  EXPECT_EQ (Disputed ("poster"), 0u);
  EXPECT_EQ (ArbiterStats ("arbiter"), Stat (0, 0));
}

TEST_F (JobsTests, BothConfirmWithoutArbiter)
{
  const auto id = PostAcceptDeal ("");
  ASSERT_TRUE (Confirm ("worker", id));
  ASSERT_TRUE (Confirm ("poster", id));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("worker"), START + 5'000 - 150);
}

TEST_F (JobsTests, Ruling)
{
  const auto id = PostAcceptDeal ();
  ASSERT_TRUE (Dispute ("poster", id));
  ASSERT_TRUE (Rule ("arbiter", id, 30));
  EXPECT_FALSE (JobExists (id));

  /* Tax (150) and fee (500) are taken from the reward.  Of the other 4350
     and the collateral, the worker gets 30% and the poster the rest.  */
  EXPECT_EQ (Balance ("worker"), START - 5'000 + 1'305 + 1'500);
  EXPECT_EQ (Balance ("poster"), START - 5'000 - 50 + 3'045 + 3'500);
  EXPECT_EQ (Balance ("arbiter"), START + 500);

  /* The value of the job for worker and poster is the reward before tax
     and fee, and for the arbiter that of reward and collateral.  */
  EXPECT_EQ (DealStats ("worker"), Stat (1, 1'500));
  EXPECT_EQ (PosterStats ("poster"), Stat (1, 1'500));
  EXPECT_EQ (Disputed ("worker"), 1u);
  EXPECT_EQ (Disputed ("poster"), 1u);
  EXPECT_EQ (ArbiterStats ("arbiter"), Stat (1, 10'000));
}

TEST_F (JobsTests, RulingOfZero)
{
  const auto id = PostAcceptDeal ();
  ASSERT_TRUE (Dispute ("worker", id));
  ASSERT_TRUE (Rule ("arbiter", id, 0));

  /* The poster gets the collateral, and the reward less tax and fee.  */
  EXPECT_EQ (Balance ("worker"), START - 5'000);
  EXPECT_EQ (Balance ("poster"), START - 5'000 - 50 + 10'000 - 150 - 500);
  EXPECT_EQ (Balance ("arbiter"), START + 500);

  /* Nothing counts as completed, but dispute and ruling are recorded.  */
  EXPECT_EQ (DealStats ("worker"), Stat (0, 0));
  EXPECT_EQ (PosterStats ("poster"), Stat (0, 0));
  EXPECT_EQ (Disputed ("worker"), 1u);
  EXPECT_EQ (Disputed ("poster"), 1u);
  EXPECT_EQ (ArbiterStats ("arbiter"), Stat (1, 10'000));
}

TEST_F (JobsTests, RulingByArbiterWithoutFee)
{
  ASSERT_TRUE (Process ("poster", R"({"t":"deal","d":86400,"r":5000,)"
                                  R"("co":5000,"arbiter":"arbiter"})"));
  const auto id = LatestJobId ();
  ASSERT_TRUE (Accept ("worker", id));
  ASSERT_TRUE (Dispute ("poster", id));
  ASSERT_TRUE (Rule ("arbiter", id, 100));

  EXPECT_EQ (Balance ("worker"), START + 5'000 - 150);
  EXPECT_EQ (Balance ("arbiter"), START);
  EXPECT_EQ (ArbiterStats ("arbiter"), Stat (1, 10'000));
}

TEST_F (JobsTests, RulingsPayTheComputedSettlement)
{
  for (int p = 0; p <= 100; p += 10)
    {
      const Amount poster = Balance ("poster");
      const Amount worker = Balance ("worker");
      const Amount arbiter = Balance ("arbiter");

      ASSERT_TRUE (Process ("poster", R"({"t":"deal","d":86400,"r":4999,)"
                                      R"("co":3333,"arbiter":"arbiter",)"
                                      R"("fee":999})"));
      const auto id = LatestJobId ();
      ASSERT_TRUE (Accept ("worker", id));
      ASSERT_TRUE (Dispute ("worker", id));
      ASSERT_TRUE (Rule ("arbiter", id, p));

      /* The posting fee is 1% of the reward, rounded down.  */
      const auto s = ComputeDealSettlement (4'999, 3'333, p, 300, 999);
      EXPECT_EQ (s.arbiter, 499);
      EXPECT_EQ (Balance ("poster"), poster - 4'999 - 49 + s.poster);
      EXPECT_EQ (Balance ("worker"), worker - 3'333 + s.worker);
      EXPECT_EQ (Balance ("arbiter"), arbiter + s.arbiter);
      EXPECT_EQ (Balance ("poster") + Balance ("worker") + Balance ("arbiter"),
                 poster + worker + arbiter - 49 - s.treasury);
    }
}

TEST_F (JobsTests, TaxFixedAtPostTime)
{
  auto id = PostAcceptDeal ();
  SetParams ([] (auto& p) { p.set_deal_tax_bps (1'000); });
  ASSERT_TRUE (Confirm ("poster", id));
  ASSERT_TRUE (Confirm ("worker", id));
  EXPECT_EQ (Balance ("worker"), START + 5'000 - 150 - 500);

  /* A job posted after the change has the new tax.  */
  id = PostAcceptDeal ();
  ASSERT_TRUE (Confirm ("poster", id));
  ASSERT_TRUE (Confirm ("worker", id));
  EXPECT_EQ (Balance ("worker"),
             START + 5'000 - 150 - 500 + 5'000 - 500 - 500);
}

TEST_F (JobsTests, NoCompletionWithoutTax)
{
  /* With a reward of 10, the tax rounds down to zero.  */
  ASSERT_TRUE (Process ("poster", R"({"t":"deal","d":86400,"r":10,"co":0})"));
  auto id = LatestJobId ();
  ASSERT_TRUE (Accept ("worker", id));
  ASSERT_TRUE (Confirm ("poster", id));
  ASSERT_TRUE (Confirm ("worker", id));
  EXPECT_EQ (Balance ("worker"), START + 10);

  /* Without tax, a ruling still counts as dispute and for the arbiter.  */
  SetParams ([] (auto& p) { p.set_deal_tax_bps (0); });
  id = PostAcceptDeal ();
  ASSERT_TRUE (Dispute ("poster", id));
  ASSERT_TRUE (Rule ("arbiter", id, 50));

  EXPECT_EQ (DealStats ("worker"), Stat (0, 0));
  EXPECT_EQ (PosterStats ("poster"), Stat (0, 0));
  EXPECT_EQ (Disputed ("worker"), 1u);
  EXPECT_EQ (Disputed ("poster"), 1u);
  EXPECT_EQ (ArbiterStats ("arbiter"), Stat (1, 10'000));
}

TEST_F (JobsTests, TaxAndFeeBelowTotal)
{
  /* Tax and fee together must be less than 10'000 basis points.  */
  SetParams ([] (auto& p) { p.set_deal_tax_bps (10'000); });
  EXPECT_FALSE (Process ("poster", R"({"t":"deal","d":86400,"r":5000,)"
                                   R"("co":0})"));

  SetParams ([] (auto& p)
    {
      p.set_deal_tax_bps (9'000);
      p.set_deal_max_fee_bps (9'999);
    });
  EXPECT_FALSE (Process ("poster", R"({"t":"deal","d":86400,"r":5000,)"
                                   R"("co":0,"arbiter":"arbiter",)"
                                   R"("fee":1000})"));
  ASSERT_TRUE (Process ("poster", R"({"t":"deal","d":86400,"r":5000,)"
                                  R"("co":0,"arbiter":"arbiter",)"
                                  R"("fee":999})"));

  const auto id = LatestJobId ();
  ASSERT_TRUE (Accept ("worker", id));
  ASSERT_TRUE (Confirm ("poster", id));
  ASSERT_TRUE (Confirm ("worker", id));
  EXPECT_EQ (Balance ("worker"), START + 5'000 - 4'500 - 499);
}

TEST_F (JobsTests, TaxAndFeeAtTypeMaxima)
{
  /* The sum of tax and fee does not overflow.  */
  constexpr uint32_t MAX = std::numeric_limits<uint32_t>::max ();
  SetParams ([] (auto& p)
    {
      p.set_deal_tax_bps (MAX);
      p.set_deal_max_fee_bps (MAX);
    });

  for (const uint32_t fee : {1u, MAX})
    EXPECT_FALSE (Process ("poster", R"({"t":"deal","d":86400,"r":5000,)"
                                     R"("co":0,"arbiter":"arbiter","fee":)"
                                       + std::to_string (fee) + "}"));
  EXPECT_EQ (jobs.CountAll (), 0);
}

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

  EXPECT_EQ (DealStats ("worker"), Stat (0, 0));
  EXPECT_EQ (PosterStats ("poster"), Stat (0, 0));
  EXPECT_EQ (Disputed ("worker"), 0u);
  EXPECT_EQ (Disputed ("poster"), 0u);
}

TEST_F (JobsTests, ExpiryAfterOneConfirmation)
{
  const auto id = PostAcceptDeal ();
  ASSERT_TRUE (Confirm ("poster", id));
  Expire ();
  EXPECT_FALSE (JobExists (id));

  EXPECT_EQ (Balance ("worker"), START + 5'000 - 150 - 500);
  EXPECT_EQ (Balance ("arbiter"), START + 500);

  EXPECT_EQ (DealStats ("worker"), Stat (1, 5'000));
  EXPECT_EQ (PosterStats ("poster"), Stat (1, 5'000));
  EXPECT_EQ (Disputed ("worker"), 0u);
  EXPECT_EQ (ArbiterStats ("arbiter"), Stat (0, 0));
}

TEST_F (JobsTests, ExpiryOfUnruledDispute)
{
  /* If a dispute is not ruled on (or there is no arbiter), both get their
     coins back, also if one of them confirmed before.  Only the dispute is
     recorded for them, and nothing for the arbiter.  */
  for (const std::string arbiter : {"arbiter", ""})
    for (const Amount co : {0, 5'000})
      {
        const auto id = PostAcceptDeal (arbiter, co);
        ASSERT_TRUE (Confirm ("worker", id));
        ASSERT_TRUE (Dispute ("poster", id));
      }
  EXPECT_EQ (Balance ("worker"), START - 2 * 5'000);
  EXPECT_EQ (Balance ("poster"), START - 4 * 5'050);
  EXPECT_EQ (Disputed ("worker"), 4u);

  Expire ();
  EXPECT_EQ (jobs.CountAll (), 0);

  EXPECT_EQ (Balance ("worker"), START);
  EXPECT_EQ (Balance ("poster"), START - 4 * 50);
  EXPECT_EQ (Balance ("arbiter"), START);

  EXPECT_EQ (DealStats ("worker"), Stat (0, 0));
  EXPECT_EQ (PosterStats ("poster"), Stat (0, 0));
  EXPECT_EQ (Disputed ("worker"), 4u);
  EXPECT_EQ (Disputed ("poster"), 4u);
  EXPECT_EQ (ArbiterStats ("arbiter"), Stat (0, 0));
  EXPECT_EQ (Disputed ("arbiter"), 0u);
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

TEST_F (JobsTests, ExpirySettlesManyJobs)
{
  constexpr unsigned n = 100;
  const Amount before = Balance ("poster") + Balance ("worker");

  for (unsigned i = 0; i < n; ++i)
    {
      ASSERT_TRUE (Process ("poster", R"({"t":"deal","d":86400,"r":10,)"
                                      R"("co":4})"));
      const auto id = LatestJobId ();
      ASSERT_TRUE (Accept ("worker", id));
      if (i % 3 == 0)
        {
          ASSERT_TRUE (Confirm ("worker", id));
        }
      else if (i % 3 == 1)
        {
          ASSERT_TRUE (Dispute ("poster", id));
        }
    }

  Expire ();
  EXPECT_EQ (jobs.CountAll (), 0);

  /* With a reward of 10, there is no tax, so only the posting fees of 1
     are gone.  */
  EXPECT_EQ (Balance ("poster") + Balance ("worker"), before - n);
}

TEST_F (JobsTests, OpsRejectedAtDeadline)
{
  const auto open = PostDeal ();
  const auto accepted = PostAcceptDeal ();
  const auto disputed = PostAcceptDeal ();
  ASSERT_TRUE (Dispute ("poster", disputed));

  ctx.SetTimestamp (BASE_TS + DAY);
  EXPECT_FALSE (Accept ("worker", open));
  EXPECT_FALSE (Assign ("poster", open, "worker"));
  EXPECT_FALSE (Cancel ("poster", open));
  EXPECT_FALSE (Confirm ("worker", accepted));
  EXPECT_FALSE (Dispute ("poster", accepted));
  EXPECT_FALSE (Rule ("arbiter", disputed, 100));

  /* All three jobs are refunded.  */
  ExpireJobs (db, ctx);
  EXPECT_EQ (jobs.CountAll (), 0);
  EXPECT_EQ (Balance ("worker"), START);
  EXPECT_EQ (Balance ("poster"), START - 3 * 50);
  EXPECT_EQ (Balance ("arbiter"), START);
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
      EXPECT_FALSE (Confirm ("", id));
      EXPECT_FALSE (Dispute ("", id));
      ASSERT_TRUE (Dispute ("worker", id));
      EXPECT_FALSE (Rule ("", id, 50));
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

TEST_F (JobsTests, DealOpsNeedAcceptedJob)
{
  const auto id = PostDeal ();
  EXPECT_FALSE (Confirm ("poster", id));
  EXPECT_FALSE (Dispute ("poster", id));
  EXPECT_FALSE (Rule ("arbiter", id, 50));
  EXPECT_FALSE (Confirm ("poster", id + 1));
}

TEST_F (JobsTests, OnlyPartiesConfirmOrDispute)
{
  const auto id = PostAcceptDeal ();
  for (const auto* name : {"arbiter", "other"})
    {
      EXPECT_FALSE (Confirm (name, id));
      EXPECT_FALSE (Dispute (name, id));
    }
}

TEST_F (JobsTests, ConfirmOnlyOnce)
{
  const auto id = PostAcceptDeal ();
  ASSERT_TRUE (Confirm ("poster", id));
  EXPECT_FALSE (Confirm ("poster", id));
  EXPECT_TRUE (JobExists (id));
}

TEST_F (JobsTests, ConfirmGivesUpOnlyOwnDispute)
{
  for (const auto& [confirmer, other] : {std::make_pair ("poster", "worker"),
                                         std::make_pair ("worker", "poster")})
    {
      const auto id = PostAcceptDeal ();
      ASSERT_TRUE (Confirm (confirmer, id));
      EXPECT_FALSE (Dispute (confirmer, id));
      EXPECT_TRUE (Dispute (other, id));
    }
}

TEST_F (JobsTests, OnlyRulingAfterDispute)
{
  const auto id = PostAcceptDeal ();
  ASSERT_TRUE (Dispute ("worker", id));
  EXPECT_FALSE (Confirm ("poster", id));
  EXPECT_FALSE (Confirm ("worker", id));
  EXPECT_FALSE (Dispute ("poster", id));
  EXPECT_TRUE (JobExists (id));
}

TEST_F (JobsTests, OnlyArbiterRulesAfterDispute)
{
  const auto id = PostAcceptDeal ();
  EXPECT_FALSE (Rule ("arbiter", id, 50));

  ASSERT_TRUE (Dispute ("poster", id));
  for (const auto* name : {"poster", "worker", "other"})
    EXPECT_FALSE (Rule (name, id, 50));

  EXPECT_TRUE (Rule ("arbiter", id, 50));
  EXPECT_FALSE (JobExists (id));
}

TEST_F (JobsTests, NoRulingWithoutArbiter)
{
  const auto id = PostAcceptDeal ("");
  ASSERT_TRUE (Dispute ("worker", id));
  for (const auto* name : {"poster", "worker", "arbiter"})
    EXPECT_FALSE (Rule (name, id, 50));
  EXPECT_TRUE (JobExists (id));
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
      R"({"dl":1,"confirm":true,"a":1})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"a":1})",

      R"({"a":1,"x":0})",
      R"({"c":1,"x":0})",
      R"({"s":1,"w":"worker","x":0})",
      R"({"dl":1,"confirm":true,"x":0})",
      R"({"t":"deal","d":100,"r":5000,"co":0,"x":0})",

      R"({"a":"1"})",
      R"({"a":0})",
      R"({"c":"1"})",
      R"({"s":"1","w":"worker"})",
      R"({"dl":"1","confirm":true})",

      R"({"s":1})",
      R"({"s":1,"w":5})",

      R"({"dl":1,"confirm":true,"dispute":true})",
      R"({"dl":1,"confirm":false})",
      R"({"dl":1,"dispute":false})",
      R"({"dl":1,"rule":55})",
      R"({"dl":1,"rule":110})",
      R"({"dl":1,"rule":-10})",
      R"({"dl":1,"rule":50.0})",

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
      R"({"dl":1,"confirm":true})",
      R"({"dl":1,"dispute":true})",
      R"({"dl":1,"rule":0})",
      R"({"dl":1,"rule":100})",
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

TEST_F (JobsTests, LimitsLoweredBelowCount)
{
  /* Lower limits stop new posts, but the jobs that exist go on.  */
  const auto id = PostDeal ();
  PostDeal ();
  SetParams ([] (auto& p)
    {
      p.set_max_live_jobs (1);
      p.set_max_jobs_per_poster (1);
    });

  const std::string post = R"({"t":"deal","d":86400,"r":5000,"co":0})";
  EXPECT_FALSE (Process ("poster", post));
  EXPECT_FALSE (Process ("other", post));

  ASSERT_TRUE (Accept ("worker", id));
  ASSERT_TRUE (Confirm ("poster", id));
  ASSERT_TRUE (Confirm ("worker", id));
  EXPECT_FALSE (JobExists (id));
  EXPECT_FALSE (Process ("other", post));

  Expire ();
  EXPECT_EQ (jobs.CountAll (), 0);
  EXPECT_TRUE (Process ("other", post));
}

/* ************************************************************************** */

TEST_F (JobsTests, LateConfirmExtendsDeadline)
{
  /* With less than the window left, a confirmation extends the deadline, so
     that the other party has the whole window to dispute.  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Confirm ("worker", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + WINDOW);

  ctx.SetTimestamp (BASE_TS + DAY);
  ExpireJobs (db, ctx);
  EXPECT_TRUE (JobExists (id));

  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("worker"), START + 5'000 - 150 - 500);
}

TEST_F (JobsTests, LateDisputeExtendsDeadline)
{
  /* With less than the window left, a dispute gives the arbiter the whole
     window to rule.  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Dispute ("poster", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + WINDOW);
  EXPECT_EQ (jobs.GetById (id)->GetProto ().dispute_time (),
             BASE_TS + DAY - 1);

  ctx.SetTimestamp (BASE_TS + DAY + 10);
  EXPECT_TRUE (Rule ("arbiter", id, 50));
  EXPECT_FALSE (JobExists (id));
}

TEST_F (JobsTests, DisputeWithoutArbiterKeepsDeadline)
{
  /* Without an arbiter, a dispute leads to the refund at the deadline,
     so that there is nothing to extend it for.  */
  const auto id = PostAcceptDeal ("", 0);
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Confirm ("worker", id));
  ctx.SetTimestamp (BASE_TS + DAY + 10);
  ASSERT_TRUE (Dispute ("poster", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + WINDOW);

  Expire ();
  EXPECT_EQ (Balance ("worker"), START);
  EXPECT_EQ (Balance ("poster"), START - 50);
}

TEST_F (JobsTests, ExtensionOnlyIfLessThanWindowLeft)
{
  const auto early = PostAcceptDeal ();
  const auto exact = PostAcceptDeal ();
  const auto late = PostAcceptDeal ();

  ASSERT_TRUE (Confirm ("worker", early));
  ctx.SetTimestamp (BASE_TS + DAY - WINDOW);
  ASSERT_TRUE (Confirm ("worker", exact));
  ctx.SetTimestamp (BASE_TS + DAY - WINDOW + 1);
  ASSERT_TRUE (Confirm ("worker", late));

  EXPECT_EQ (Deadline (early), BASE_TS + DAY);
  EXPECT_EQ (Deadline (exact), BASE_TS + DAY);
  EXPECT_EQ (Deadline (late), BASE_TS + DAY + 1);
}

TEST_F (JobsTests, AtMostTwoExtensions)
{
  /* A late confirmation and a late dispute each extend the deadline.  After
     that, only the ruling is left, which settles the job.  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Confirm ("worker", id));
  ctx.SetTimestamp (Deadline (id) - 1);
  ASSERT_TRUE (Dispute ("poster", id));

  const int64_t last = BASE_TS + DAY - 2 + 2 * WINDOW;
  EXPECT_EQ (Deadline (id), last);
  EXPECT_LT (Deadline (id), BASE_TS + DAY + 2 * WINDOW);

  ctx.SetTimestamp (last - 1);
  EXPECT_FALSE (Confirm ("poster", id));
  EXPECT_FALSE (Confirm ("worker", id));
  EXPECT_FALSE (Dispute ("poster", id));
  EXPECT_FALSE (Dispute ("worker", id));
  EXPECT_EQ (Deadline (id), last);

  EXPECT_TRUE (Rule ("arbiter", id, 50));
  EXPECT_FALSE (JobExists (id));
}

TEST_F (JobsTests, LateAcceptKeepsDeadline)
{
  const auto id = PostDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Accept ("worker", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY);

  ASSERT_TRUE (Confirm ("worker", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + WINDOW);
}

TEST_F (JobsTests, ReactionWindowFixedAtPostTime)
{
  const auto before = PostAcceptDeal ();
  SetParams ([] (auto& p) { p.set_deal_reaction_window (0); });
  const auto after = PostAcceptDeal ();
  SetParams ([] (auto& p) { p.set_deal_reaction_window (2 * WINDOW); });

  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Confirm ("worker", before));
  ASSERT_TRUE (Confirm ("worker", after));
  EXPECT_EQ (Deadline (before), BASE_TS + DAY - 1 + WINDOW);
  EXPECT_EQ (Deadline (after), BASE_TS + DAY);
}

TEST_F (JobsTests, ReactionWindowAtMostDuration)
{
  /* The window is at most the duration of the job, and at least zero.  */
  SetParams ([] (auto& p) { p.set_deal_reaction_window (2 * DAY); });
  auto id = PostDeal ();
  EXPECT_EQ (jobs.GetById (id)->GetProto ().reaction_window (), DAY);

  SetParams ([] (auto& p) { p.set_deal_reaction_window (-5); });
  id = PostDeal ();
  EXPECT_EQ (jobs.GetById (id)->GetProto ().reaction_window (), 0);
}

} // anonymous namespace
} // namespace pxd
