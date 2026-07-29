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

#include "gamestatejson.hpp"
#include "jsonutils.hpp"
#include "testutils.hpp"

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/params.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace pxd
{
namespace
{

/** A comfortable base consensus timestamp for the tests.  */
constexpr int64_t BASE_TS = 1000000;
/** A comfortable listing-window length in seconds.  */
constexpr int64_t DAY = 86400;

/* ************************************************************************** */

class JobsTests : public DBTestWithSchema
{

private:

  AccountsTable::Handle
  GetAccount (const std::string& name)
  {
    auto a = accounts.GetByName (name);
    if (a == nullptr)
      return accounts.CreateNew (name);
    return a;
  }

protected:

  AccountsTable accounts;
  BuildingsTable buildings;
  CharacterTable characters;
  JobsTable jobs;
  ParamsTable params;

  ContextForTesting ctx;

  JobsTests ()
    : accounts(db), buildings(db), characters(db), jobs(db), params(db)
  {
    ctx.SetHeight (100);
    ctx.SetTimestamp (BASE_TS);

    MakeAccount ("poster", Faction::RED, 1000000);
    MakeAccount ("courier", Faction::RED, 1000000);
    MakeAccount ("courier2", Faction::RED, 1000000);
    MakeAccount ("green", Faction::GREEN, 1000000);

    /* Building 1 = own faction (valid destination), 2 = neutral/ancient
       (valid), 3 = enemy faction (invalid destination for a RED poster).  */
    CHECK_EQ (buildings.CreateNew ("checkmark", "poster", Faction::RED)
                  ->GetId (), 1);
    CHECK_EQ (buildings.CreateNew ("checkmark", "", Faction::ANCIENT)
                  ->GetId (), 2);
    CHECK_EQ (buildings.CreateNew ("checkmark", "green", Faction::GREEN)
                  ->GetId (), 3);

    /* The minimum-reward floors are runtime parameters (roconfig defaults
       100 and 1000 vCHI); the tests use small rewards throughout, so lower
       them exactly as an admin would.  The roconfig-default fallback itself
       is exercised end-to-end by jobs_caps.py.  */
    params.Set ("min-job-reward", 1);
    params.Set ("min-deal-reward", 1);
  }

  JobContext
  Ctx ()
  {
    return {ctx, accounts, jobs, params};
  }

  std::unique_ptr<JobOperation>
  Parse (Account& a, const Json::Value& op)
  {
    const JobContext jc = Ctx ();
    return JobOperation::Parse (a, op, jc);
  }

  void
  MakeAccount (const std::string& name, const Faction f, const Amount bal)
  {
    auto a = accounts.CreateNew (name);
    a->SetFaction (f);
    a->AddBalance (bal);
  }

  Amount
  Balance (const std::string& name)
  {
    auto a = accounts.GetByName (name);
    return a == nullptr ? 0 : a->GetBalance ();
  }

  /** Returns whether the JSON string parses to a well-formed operation.  */
  bool
  ParseOk (const std::string& data)
  {
    auto a = GetAccount ("poster");
    return Parse (*a, ParseJson (data)) != nullptr;
  }

  /**
   * Parses, validates and (if valid) executes an operation for the account.
   * The format must be valid.  Returns whether it was valid + executed.
   */
  bool
  Process (const std::string& name, const std::string& data)
  {
    auto a = GetAccount (name);
    auto op = Parse (*a, ParseJson (data));
    CHECK (op != nullptr) << "Format invalid: " << data;
    if (!op->IsValid ())
      return false;
    op->Execute ();
    return true;
  }

  /** Returns the ID of the single job currently in the table.  */
  Database::IdT
  OnlyJobId ()
  {
    auto res = jobs.QueryAll ();
    CHECK (res.Step ());
    const auto id = jobs.GetFromResult (res)->GetId ();
    CHECK (!res.Step ());
    return id;
  }

  /** Returns the highest job ID in the table (i.e. the one just posted).  */
  Database::IdT
  LatestJobId ()
  {
    Database::IdT best = 0;
    auto res = jobs.QueryAll ();
    while (res.Step ())
      {
        const auto id = jobs.GetFromResult (res)->GetId ();
        if (id > best)
          best = id;
      }
    CHECK_GT (best, 0);
    return best;
  }

  bool
  JobExists (const Database::IdT id)
  {
    return jobs.GetById (id) != nullptr;
  }

};
class DealTests : public JobsTests
{

protected:

  /** Returns (deals_completed, deals_value_completed) for an account.  */
  std::pair<unsigned, Amount>
  DealStats (const std::string& name)
  {
    const auto& pb = accounts.GetByName (name)->GetProto ();
    return {pb.deals_completed (), pb.deals_value_completed ()};
  }

  /** Returns the POSTER mirror (deals_posted_completed, deals_posted_value).
      Gated on the same creditRep bool as DealStats above, so the treasury>=1
      anti-wash boundary needs no separate case: both sides share one gate.  */
  std::pair<unsigned, Amount>
  PosterStats (const std::string& name)
  {
    const auto& pb = accounts.GetByName (name)->GetProto ();
    return {pb.deals_posted_completed (), pb.deals_posted_value ()};
  }

  /** Returns deals_disputed for an account.  */
  unsigned
  Disputed (const std::string& name)
  {
    return accounts.GetByName (name)->GetProto ().deals_disputed ();
  }

  /** Returns (arbiter_rulings, arbiter_value_ruled) for an account.  There is
      deliberately no ghost counter to read: see BumpArbiterRulingStats.  */
  std::pair<unsigned, Amount>
  ArbiterStats (const std::string& name)
  {
    const auto& pb = accounts.GetByName (name)->GetProto ();
    return {pb.arbiter_rulings (), pb.arbiter_value_ruled ()};
  }

  /** The pot (reward + collateral) of the standard fixture deal, which is what
      a ruling on it credits to the arbiter's value counter.  */
  static constexpr Amount STD_POT = 5000 + 5000;

  /** Posts a standard deal (reward 5000, collateral 5000, arbiter courier2,
      fee 10%).  Pass arbiter="" for a no-arbiter deal.  Returns its id.  */
  Database::IdT
  PostDeal (const std::string& arbiter = "courier2", const Amount co = 5000)
  {
    std::string t
        = R"({"t":"deal","d":86400,"r":5000,"co":)" + std::to_string (co)
          + R"(,"tag":1,"terms":"haul it")";
    if (!arbiter.empty ())
      t += R"(,"arbiter":")" + arbiter + R"(","fee":1000)";
    t += "}";
    CHECK (Process ("poster", t));
    return LatestJobId ();
  }

  /** Posts + has courier accept.  Returns the deal id.  */
  Database::IdT
  PostAcceptDeal (const std::string& arbiter = "courier2",
                  const Amount co = 5000)
  {
    const auto id = PostDeal (arbiter, co);
    CHECK (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
    return id;
  }

  /** Advances the clock past the deal deadline and runs the expiry sweep.  */
  void
  Expire ()
  {
    ctx.SetTimestamp (BASE_TS + DAY + 50);
    ExpireJobs (db, ctx);
  }

  /** Renders the live board JSON row for a job id (null if it is not live).  */
  Json::Value
  LiveJson (const Database::IdT id)
  {
    GameStateJson gsj(db, ctx);
    const Json::Value arr = gsj.Jobs ();
    for (const auto& e : arr)
      if (e["id"].asUInt64 () == static_cast<Json::UInt64> (id))
        return e;
    return Json::Value ();
  }

  /**
   * Asserts the terminal state of a no-arbiter GHOST_SPLIT on the standard
   * 5000/5000 deal at the default 3% tax.  p=50 pays the worker 4925 (its 2500
   * reward share minus 75 tax plus its 2500 returned collateral), returns the
   * poster the remaining 4850 (its 5000 reward and 5000 collateral, less the
   * worker's 4925 and the 225 burned tax), and burns 225 to the treasury.  The
   * history records mode ghost-split at settledp 50 with NO feepaid key -- no
   * arbiter was ever bound, so no fee schedule exists to honour or forfeit.
   */
  void
  ExpectNoArbiterGhostSplit (const Database::IdT id)
  {
    EXPECT_FALSE (JobExists (id));
    EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 4925);
    EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50 + 4850);
    /* THE trap in the arbiter record: GHOST_SPLIT is also how a deal that
       never named an arbiter settles a dispute, so no arbiter counter may move
       here.  courier2 (the arbiter of every OTHER deal in this fixture) is a
       bystander to these and must stay untouched -- otherwise a ghosting
       penalty would land on whoever happens to arbitrate elsewhere.  Both
       parties still carry the dispute itself.  */
    EXPECT_EQ (ArbiterStats ("courier2"),
               std::make_pair (0u, static_cast<Amount> (0)));
    EXPECT_EQ (Disputed ("poster"), 1u);
    EXPECT_EQ (Disputed ("courier"), 1u);
  }

  /** The deal actions as one-liners (all through validate + execute).  */
  bool Confirm (const std::string& who, const Database::IdT id)
  {
    return Process (who,
        R"({"dl":)" + std::to_string (id) + R"(,"confirm":true})");
  }
  bool Dispute (const std::string& who, const Database::IdT id)
  {
    return Process (who,
        R"({"dl":)" + std::to_string (id) + R"(,"dispute":true})");
  }
  bool Rule (const std::string& who, const Database::IdT id, const int p)
  {
    return Process (who, R"({"dl":)" + std::to_string (id) + R"(,"rule":)"
                          + std::to_string (p) + "}");
  }

  /** The live deadline / snapshotted reaction window of a deal row.  */
  int64_t Deadline (const Database::IdT id)
  { return jobs.GetById (id)->GetDeadline (); }
  int64_t Rwindow (const Database::IdT id)
  { return jobs.GetById (id)->GetProto ().reaction_window (); }

};

TEST_F (DealTests, HappyPathBothConfirm)
{
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (JobExists (id));    // one confirm: still open
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_FALSE (JobExists (id));   // both confirmed: settled + deleted
  /* p=100: worker <- 5000 - 150(tax) - 500(fee) + 5000(collateral) = 9350;
     arbiter <- 500; treasury 150 burned; poster <- 0.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9350);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 500);
  EXPECT_EQ (DealStats ("courier"), std::make_pair (1u, static_cast<Amount> (5000)));
  /* The poster's mirror of the same settlement, and a clean deal that never
     troubled the arbiter: no dispute on either party, no arbiter record.  */
  EXPECT_EQ (PosterStats ("poster"), std::make_pair (1u, static_cast<Amount> (5000)));
  EXPECT_EQ (Disputed ("poster"), 0u);
  EXPECT_EQ (Disputed ("courier"), 0u);
  EXPECT_EQ (ArbiterStats ("courier2"),
             std::make_pair (0u, static_cast<Amount> (0)));
}

TEST_F (DealTests, DisputeArbiterRulesPartial)
{
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_TRUE (Rule ("courier2", id, 30));
  EXPECT_FALSE (JobExists (id));
  /* p=30: worker 2805, poster 6090, arbiter 850, treasury 255.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 2805);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 850);
  /* All three records move: both sides carry the dispute, the arbiter carries
     the ruling it actually delivered, and both value counters scale with p
     (5000 * 30/100), so a low ruling credits proportionally less.  */
  EXPECT_EQ (DealStats ("courier"), std::make_pair (1u, static_cast<Amount> (1500)));
  EXPECT_EQ (PosterStats ("poster"), std::make_pair (1u, static_cast<Amount> (1500)));
  EXPECT_EQ (Disputed ("poster"), 1u);
  EXPECT_EQ (Disputed ("courier"), 1u);
  /* The arbiter's value counter takes the whole POT it directed, not the
     worker's share: a p=30 ruling decided the fate of all 10000 just as much
     as a p=100 one would.  */
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (1u, STD_POT));
}

TEST_F (DealTests, DisputeArbiterRulesFullCompletion)
{
  /* The top of the ruling range, which the other ruling tests skip: p=100
     pays out exactly as a both-confirm does (the poster's transacted share is
     zero, so it bears no tax and no fee), but through the RULING path -- so the
     history mode differs and the arbiter's record moves.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Dispute ("courier", id));
  EXPECT_TRUE (Rule ("courier2", id, 100));
  EXPECT_FALSE (JobExists (id));
  /* p=100: worker 5000 - 150(tax) - 500(fee) + 5000(collateral) = 9350;
     arbiter 500; treasury 150 burned; poster 0 back beyond its posting fee.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9350);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 500);
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50);
  EXPECT_EQ (DealStats ("courier"), std::make_pair (1u, static_cast<Amount> (5000)));
  EXPECT_EQ (PosterStats ("poster"), std::make_pair (1u, static_cast<Amount> (5000)));
  /* A full-completion ruling is still a dispute for both parties -- the deal
     needed one to end, which is exactly what the counter says.  */
  EXPECT_EQ (Disputed ("poster"), 1u);
  EXPECT_EQ (Disputed ("courier"), 1u);
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (1u, STD_POT));
}

TEST_F (DealTests, TimeoutGhostSplits5050)
{
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Dispute ("courier", id));
  Expire ();
  EXPECT_FALSE (JobExists (id));
  /* p=50 with the fee FORFEITED (the arbiter ghosted the one dispute it was
     hired to rule): worker 2500 - 75(tax) + 2500(collateral) = 4925; the
     arbiter gets nothing; treasury 225 burned; poster keeps its fee share
     too.  Ruling must always pay the arbiter better than ghosting.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 4925);
  EXPECT_EQ (Balance ("courier2"), 1000000);
  /* L3: a ghost split is a tax-bearing settlement with p>0, so it DOES bump
     the worker's deal record -- deals_completed counts these (not just clean
     completions), and the value tracks the earned reward share R*50/100 = 2500,
     not the full 5000 reward.  Phase-2 reputation must read the counter this
     way.  */
  EXPECT_EQ (DealStats ("courier"),
             std::make_pair (1u, static_cast<Amount> (2500)));
  /* The poster's mirror follows the same p.  */
  EXPECT_EQ (PosterStats ("poster"), std::make_pair (1u, static_cast<Amount> (2500)));
  EXPECT_EQ (Disputed ("poster"), 1u);
  EXPECT_EQ (Disputed ("courier"), 1u);
  /* This IS an arbiter ghost, and it must leave NO mark on the arbiter's
     account record -- the poster bound courier2 unilaterally, so a permanent
     counter here would be inflictable on any non-consenting account (see
     ArbiterGhostLeavesNoMarkOnNonConsentingAccount).  The ghost is still fully
     attributable from the history row above: mode ghost-split with
     feepaid=false, plus the stamped disputetime.  The fee forfeiture is the
     punishment consensus does apply.  */
  EXPECT_EQ (ArbiterStats ("courier2"),
             std::make_pair (0u, static_cast<Amount> (0)));
}

TEST_F (DealTests, ArbiterGhostLeavesNoMarkOnNonConsentingAccount)
{
  /* A post binds an arbiter UNILATERALLY: it names any initialised account and
     there is no consent move, no acceptance, no rejection and no revocation --
     the named account need not even know the deal exists.  So two colluding
     accounts can drive any third one through a full dispute-and-ghost cycle for
     the price of the burn on one throwaway deal.  That is precisely why the
     account proto carries no ghost counter: it would be a permanent, publicly
     queried mark inflictable on a bystander, and consensus has no way to tell
     an unwilling arbiter from a negligent one.

     green is the bystander here.  It signs nothing -- every move below is sent
     by poster or courier -- and its record must be untouched afterwards, over
     both fee shapes (a fee makes the arbiter a payee on the happy path, so it
     is the case most likely to reach for the account row).  */
  int64_t clock = BASE_TS;
  for (const std::string& fee : {std::string (), std::string (R"(,"fee":1000)")})
    {
      ctx.SetTimestamp (clock);
      ASSERT_TRUE (Process ("poster",
          R"({"t":"deal","d":86400,"r":1000,"co":0,"terms":"bait",)"
          R"("arbiter":"green")" + fee + "}"));
      const auto id = LatestJobId ();
      ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
      ASSERT_TRUE (Dispute ("poster", id));

      clock += DAY + 50;
      ctx.SetTimestamp (clock);
      ExpireJobs (db, ctx);
      ASSERT_FALSE (JobExists (id));
    }

  EXPECT_EQ (ArbiterStats ("green"),
             std::make_pair (0u, static_cast<Amount> (0)));
  EXPECT_EQ (Disputed ("green"), 0u);
  EXPECT_EQ (DealStats ("green"), std::make_pair (0u, static_cast<Amount> (0)));
  EXPECT_EQ (PosterStats ("green"), std::make_pair (0u, static_cast<Amount> (0)));
  /* Nor does a ghosted arbiter collect anything: the fee is forfeited, so the
     only balance that moved is the colluders' burn.  */
  EXPECT_EQ (Balance ("green"), 1000000);
}

TEST_F (DealTests, TimeoutSingleConfirmPaysWorker)
{
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("courier", id));
  Expire ();
  EXPECT_FALSE (JobExists (id));
  /* one confirm at timeout => p=100.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9350);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 500);
  /* A quiet timeout is not a dispute: the poster's payout is on record, but
     neither party carries a dispute and the arbiter was never called on.  */
  EXPECT_EQ (PosterStats ("poster"), std::make_pair (1u, static_cast<Amount> (5000)));
  EXPECT_EQ (Disputed ("poster"), 0u);
  EXPECT_EQ (Disputed ("courier"), 0u);
  EXPECT_EQ (ArbiterStats ("courier2"),
             std::make_pair (0u, static_cast<Amount> (0)));
}

TEST_F (DealTests, TimeoutNeitherConfirmRefundsBoth)
{
  const auto id = PostAcceptDeal ();
  Expire ();
  EXPECT_FALSE (JobExists (id));
  /* Refund both, arbiter bound: worker <- full collateral, poster <- full
     reward (only the posting fee was burned at post), arbiter untouched;
     nothing is taxed or forfeited at settlement.  M1: the untaxed
     neither-acted refund is the pinned behaviour.  */
  EXPECT_EQ (Balance ("courier"), 1000000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 50);
  EXPECT_EQ (Balance ("courier2"), 1000000);
  /* A no-fault refund moves NO record at all, on any of the three roles --
     nobody worked, nobody was paid, nobody was judged.  */
  EXPECT_EQ (DealStats ("courier"), std::make_pair (0u, static_cast<Amount> (0)));
  EXPECT_EQ (PosterStats ("poster"), std::make_pair (0u, static_cast<Amount> (0)));
  EXPECT_EQ (Disputed ("poster"), 0u);
  EXPECT_EQ (Disputed ("courier"), 0u);
  EXPECT_EQ (ArbiterStats ("courier2"),
             std::make_pair (0u, static_cast<Amount> (0)));
}

TEST_F (DealTests, TimeoutNeitherConfirmRefundsBothNoArbiter)
{
  /* M1, no-arbiter variant: worker <- full collateral, poster <- full reward,
     nothing burned at settlement; history mode refund with no feepaid key
     (no arbiter bound) and no settledp.  */
  const auto id = PostAcceptDeal ("");
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 50);
}

TEST_F (DealTests, NoArbiterHappyPath)
{
  const auto id = PostAcceptDeal ("");
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_FALSE (JobExists (id));
  /* p=100, no arbiter fee: worker <- 5000 - 150 + 5000 = 9850.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9850);
}

TEST_F (DealTests, RuleOnlyByArbiterAfterDispute)
{
  const auto id = PostAcceptDeal ();
  EXPECT_FALSE (Rule ("courier2", id, 50));  // no dispute yet
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_FALSE (Rule ("poster", id, 50));   // not the arbiter
  EXPECT_FALSE (Rule ("courier", id, 50));  // not the arbiter
  EXPECT_TRUE (Rule ("courier2", id, 50));  // the bound arbiter
  EXPECT_FALSE (JobExists (id));
}

TEST_F (DealTests, CollateralCapRejected)
{
  /* collateral 3000 > 2x reward 1000 (deal-max-collateral-bps default 20000).  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":1000,"co":3000,"terms":"x"})"));
  /* within the cap is fine.  */
  EXPECT_TRUE (Process ("poster",
      R"({"t":"deal","d":86400,"r":1000,"co":2000,"terms":"x"})"));
}

TEST_F (DealTests, RejectsUnknownKeysAndBadGrammar)
{
  /* The removed "wd" key is no longer in the grammar (a deal never took one).  */
  EXPECT_FALSE (ParseOk (
      R"({"t":"deal","d":86400,"wd":3600,"r":5000,"co":5000})"));
  /* Unknown post key rejected (strict grammar).  */
  EXPECT_FALSE (ParseOk (
      R"({"t":"deal","d":86400,"r":5000,"co":5000,"bogus":1})"));
  /* A rule p must be a multiple of 10 in [0,100].  */
  EXPECT_FALSE (ParseOk (R"({"dl":1,"rule":35})"));
  EXPECT_FALSE (ParseOk (R"({"dl":1,"rule":110})"));
  EXPECT_TRUE (ParseOk (R"({"dl":1,"rule":40})"));
}

TEST_F (DealTests, SettlementConservesExhaustive)
{
  /* Pin ComputeDealSettlement's conservation + non-negativity in the suite.  */
  for (const Amount R : {static_cast<Amount> (0), static_cast<Amount> (1),
                         static_cast<Amount> (1000), static_cast<Amount> (50000),
                         static_cast<Amount> (100000000000LL)})
    for (const Amount C : {static_cast<Amount> (0), static_cast<Amount> (7),
                           static_cast<Amount> (999), static_cast<Amount> (50000)})
      for (const int t : {0, 300, 1000})
        for (const int f : {0, 500, 1000})
          for (int p = 0; p <= 100; p += 10)
            {
              const auto s = ComputeDealSettlement (R, C, p, t, f);
              EXPECT_EQ (s.worker + s.poster + s.arbiter + s.treasury, R + C);
              EXPECT_GE (s.worker, 0);
              EXPECT_GE (s.poster, 0);
            }
}

TEST_F (DealTests, PosterEqualsArbiterPostRejected)
{
  /* §1: poster == arbiter is an armed trap under the reaction window (worker
     confirms late -> poster-arbiter disputes inside the extension -> rules p=0
     for a total seizure v1's hard deadline capped at 50/50), banned at the post
     door.  Nothing is charged or created.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":5000,"arbiter":"poster","fee":1000})"));
  EXPECT_EQ (jobs.CountAll (), 0);
}

TEST_F (DealTests, CannotConfirmTwice)
{
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_FALSE (Confirm ("poster", id));
  EXPECT_TRUE (JobExists (id));   // still open on one confirm
}

TEST_F (DealTests, ZeroCollateralHappyPath)
{
  const auto id = PostAcceptDeal ("courier2", 0);
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_FALSE (JobExists (id));
  /* p=100, C=0: worker <- 5000 - 150 - 500 = 4350; arbiter <- 500.  */
  EXPECT_EQ (Balance ("courier"), 1000000 + 4350);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 500);
}

TEST_F (DealTests, PostRejectsFeeWithoutArbiter)
{
  /* A fee with no arbiter to earn it (missing or empty member) is rejected,
     never silently dropped: the poster most likely mistyped the arbiter.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"fee":1000})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"arbiter":"","fee":1000})"));
  /* An empty arbiter alone is likewise a meaningless member.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"arbiter":""})"));
  /* An arbiter without a fee is fine (a pro-bono arbiter).  */
  EXPECT_TRUE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"arbiter":"courier2"})"));
}

TEST_F (DealTests, PostRejectsTaxFeeBeyondPrecondition)
{
  /* The §6.3 precondition (0 <= tax, tax + fee < 10000) is enforced on the
     values a post would snapshot, so a misconfigured runtime retune rejects
     NEW deals instead of freezing bps into rows whose later settlement
     would CHECK-halt every node.  */
  params.Set ("deal-tax-bps", 12'000);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"terms":"x"})"));
  params.Set ("deal-tax-bps", -5);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"terms":"x"})"));

  /* tax + fee exactly 10000 is rejected; strictly below passes and settles
     without tripping any settlement CHECK.  */
  params.Set ("deal-tax-bps", 9'000);
  params.Set ("deal-max-fee-bps", 9'999);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,)"
      R"("arbiter":"courier2","fee":1000})"));
  EXPECT_TRUE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,)"
      R"("arbiter":"courier2","fee":999})"));
  const auto id = LatestJobId ();
  CHECK (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_FALSE (JobExists (id));
  /* p=100: worker <- 5000 - 4500(tax) - 499(fee) = 1; every coin conserved
     by the settlement identity.  */
  EXPECT_EQ (Balance ("courier"), 1000000 + 1);
}

TEST_F (DealTests, LifecycleOpsRejectedAtDeadline)
{
  /* Confirm, dispute and rule are the sweep's to reject once the end date
     is reached (JobIsDue), like every other lifecycle op: otherwise a
     confirm in the deadline-to-sweep gap would flip a settlement already
     fixed by the state at the deadline.  JobIsDue uses the exclusive boundary
     (deadline <= now), so the timestamp here is set EXACTLY to the deadline
     to pin that all three ops are rejected at now == deadline.  Here neither
     party acted, so the sweep must refund BOTH stakes despite the late
     confirm attempt.  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY);
  EXPECT_FALSE (Confirm ("courier", id));
  EXPECT_FALSE (Dispute ("poster", id));
  EXPECT_FALSE (Rule ("courier2", id, 100));
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000);
  EXPECT_EQ (Balance ("courier2"), 1000000);
}

TEST_F (DealTests, ArbiterCannotAccept)
{
  /* The arbiter must stay a third party: as worker it would judge its own
     dispute (accept + self-dispute + self-rule p=100 in one block would
     capture the whole escrow).  */
  const auto id = PostDeal ();
  EXPECT_FALSE (Process ("courier2", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
}

TEST_F (DealTests, ConfirmRejectedWhileDisputed)
{
  /* Once disputed, only the arbiter's ruling (or the sweep's 50/50) settles
     the deal -- the documented DealPayload invariant.  A both-confirm racing
     the ruling must not bypass the arbiter.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Dispute ("courier", id));
  EXPECT_FALSE (Confirm ("courier", id));
  EXPECT_TRUE (JobExists (id));
  EXPECT_TRUE (Rule ("courier2", id, 50));
  EXPECT_FALSE (JobExists (id));
}

TEST_F (DealTests, PostTermBoundaries)
{
  /* The optional term bounds are inclusive: tag <= 100, terms <= 1000 bytes,
     dp <= 100; one past each rejects.  */
  EXPECT_TRUE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"tag":100,"dp":100})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"tag":101})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"dp":101})"));
  const std::string maxTerms(1'000, 'x');
  EXPECT_TRUE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"terms":")" + maxTerms
        + R"("})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"terms":")" + maxTerms
        + R"(x"})"));
}

TEST_F (DealTests, RuleZeroFailsWorkerWithoutReputation)
{
  /* p=0: the worker earns nothing and forfeits the whole collateral; the
     ruling arbiter still earns its fee (it did the job); the outcome is
     FAILED and no reputation is credited.  R=5000 C=5000 t=300 f=1000:
     posterTransacted=10000 -> tax 300, fee 1000, poster 8700.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_TRUE (Rule ("courier2", id, 0));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 1000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50 + 8700);
  EXPECT_EQ (DealStats ("courier"), std::make_pair (0u, static_cast<Amount> (0)));
  /* The earning gate (p>0) denies BOTH sides their completion record here --
     nothing was delivered and nothing paid out.  The dispute and the arbiter's
     ruling are recorded regardless: the arbiter did the work it was hired for,
     and that must not depend on which way it ruled.  */
  EXPECT_EQ (PosterStats ("poster"), std::make_pair (0u, static_cast<Amount> (0)));
  EXPECT_EQ (Disputed ("poster"), 1u);
  EXPECT_EQ (Disputed ("courier"), 1u);
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (1u, STD_POT));
  /* A p=0 ruling still records the actual ruling (settledp 0) and the paid
     fee; the outcome stays "failed" (the client renders it neutrally).  */
}

TEST_F (DealTests, NonPartiesCannotTouch)
{
  /* Only the two parties may confirm or dispute (and only once); green is
     a complete stranger to this deal.  */
  const auto id = PostAcceptDeal ();
  EXPECT_FALSE (Confirm ("green", id));
  EXPECT_FALSE (Dispute ("green", id));
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_FALSE (Dispute ("courier", id));
  EXPECT_TRUE (JobExists (id));
}

TEST_F (DealTests, ZeroWindowDealSweepsVoid)
{
  /* d=0 posts a deal that is due the moment it exists: no lifecycle op can
     touch it (JobIsDue) and the sweep voids it with a full reward refund --
     the poster only burns the posting fee.  */
  ASSERT_TRUE (Process ("poster",
      R"({"t":"deal","d":0,"r":5000,"co":1000,"terms":"instant"})"));
  const auto id = LatestJobId ();
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 50);
}

TEST_F (DealTests, MinDealRewardFloor)
{
  params.Set ("min-deal-reward", 2'000);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":1500,"co":0,"terms":"small"})"));
  EXPECT_TRUE (Process ("poster",
      R"({"t":"deal","d":86400,"r":2000,"co":0,"terms":"exact"})"));
}

TEST_F (DealTests, SweepSettlesManyDealsAtOnce)
{
  /* A moderate-scale sweep: many deals in mixed lifecycle states all hit
     their shared deadline and settle in ONE ExpireJobs call, exercising the
     batch path (row deletion, history writes, per-name credit) end to end.
     Balances must conserve exactly across the whole cohort.  */
  constexpr unsigned N = 200;
  Amount before = 0;
    CHECK_EQ (jobs.CountAll (), 0);
  for (const auto* name : {"poster", "courier", "courier2", "green"})
    before += Balance (name);

  for (unsigned i = 0; i < N; ++i)
    {
      ASSERT_TRUE (Process ("poster",
          R"({"t":"deal","d":86400,"r":10,"co":4,"tag":1})"));
      const auto id = LatestJobId ();
      const std::string dl = std::to_string (id);
      ASSERT_TRUE (Process ("courier", R"({"a":)" + dl + "}"));
      switch (i % 3)
        {
        case 0:   /* one confirm -> p=100 at the sweep */
          ASSERT_TRUE (Confirm ("courier", id));
          break;
        case 1:   /* disputed, no arbiter -> 50/50 at the sweep */
          ASSERT_TRUE (Dispute ("poster", id));
          break;
        case 2:   /* untouched -> both stakes refund */
          break;
        }
    }

  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);

    EXPECT_EQ (jobs.CountAll (), 0);
  Amount after = 0;
  for (const auto* name : {"poster", "courier", "courier2", "green"})
    after += Balance (name);
  /* Everything escrowed came back out except the burned posting fees
     (N x 1, the minimum fee) and the settlement taxes: p=100 deals burn
     nothing here (10*300/10000 = 0 per share), the 50/50 ones likewise
     round to zero -- so exactly the fees are gone.  */
  EXPECT_EQ (before - after, static_cast<Amount> (N));
}

TEST_F (DealTests, ConfirmBarsOwnDisputePoster)
{
  /* H1: a confirmation waives only the confirmer's OWN dispute right, so the
     poster cannot revoke its confirm by disputing afterwards.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_FALSE (Dispute ("poster", id));
  EXPECT_TRUE (JobExists (id));
  EXPECT_FALSE (jobs.GetById (id)->GetProto ().disputed ());
}

TEST_F (DealTests, ConfirmBarsOwnDisputeWorker)
{
  /* H1, the worker's mirror image of the guard.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_FALSE (Dispute ("courier", id));
  EXPECT_TRUE (JobExists (id));
  EXPECT_FALSE (jobs.GetById (id)->GetProto ().disputed ());
}

TEST_F (DealTests, ConfirmerCounterpartyMayStillDispute)
{
  /* H1: the poster's confirm does NOT waive the worker's dispute right -- the
     counterparty may still contest a shoddy job.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Dispute ("courier", id));
  EXPECT_TRUE (jobs.GetById (id)->GetProto ().disputed ());
}

TEST_F (DealTests, WorkerConfirmPosterMayStillDispute)
{
  /* H1: the worker's confirm does NOT waive the poster's dispute right.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_TRUE (jobs.GetById (id)->GetProto ().disputed ());
}

TEST_F (DealTests, NoArbiterConfirmerCannotForceGhostSplit)
{
  /* H1, no-arbiter: a party that confirmed cannot then dispute to drag an
     honest deal into the 50/50 ghost split; the confirm stands and the sweep
     settles SINGLE_CONFIRM at p=100 (NOT the 4925 of a 50/50 split).  */
  const auto id = PostAcceptDeal ("");
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_FALSE (Dispute ("courier", id));
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9850);
}

TEST_F (DealTests, PostRejectsOverflowTaxFee)
{
  /* L1: tax and fee are each bounded in [0, 9999] BEFORE their sum, so a
     runtime param near 2^62 (whose low 32 bits are non-zero, thus narrowing
     to a small uint32) cannot overflow the signed sum past the precondition
     guard and freeze a settlement-halting pair into a row.  No row is
     created.  2^62 alone would be insufficient (low 32 bits zero).  */
  constexpr int64_t BIG = (static_cast<int64_t> (1) << 62) + 6000;
  const std::string big = std::to_string (BIG);
  params.Set ("deal-tax-bps", BIG);
  params.Set ("deal-max-fee-bps", BIG);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,)"
      R"("arbiter":"courier2","fee":)" + big + "}"));

  /* INT64_MAX for both is likewise rejected.  */
  const int64_t MAX = std::numeric_limits<int64_t>::max ();
  const std::string maxStr = std::to_string (MAX);
  params.Set ("deal-tax-bps", MAX);
  params.Set ("deal-max-fee-bps", MAX);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,)"
      R"("arbiter":"courier2","fee":)" + maxStr + "}"));

  /* Nothing was admitted to the board.  */
  EXPECT_EQ (jobs.CountAll (), 0);
}

TEST_F (DealTests, AssignRestrictsAcceptToDesignatedWorker)
{
  /* L3: assignment makes a PUBLIC deal exclusive -- the poster designates a
     worker and only that worker may accept.  It does NOT make the row
     born-private: invite_only stays unset (that bit is the POST-time
     discriminator), so the exported exclusivity predicate is the pair
     `inviteonly || designated != ""` and this row carries only the second.  */
  const auto id = PostDeal ();
  const std::string sid = std::to_string (id);
  EXPECT_TRUE (Process ("poster", R"({"s":)" + sid + R"(,"w":"courier"})"));
  EXPECT_EQ (LiveJson (id)["designated"].asString (), "courier");
  EXPECT_FALSE (LiveJson (id).isMember ("inviteonly"));
  EXPECT_FALSE (Process ("green", R"({"a":)" + sid + "}"));   // not designated
  EXPECT_TRUE (JobExists (id));
  EXPECT_TRUE (Process ("courier", R"({"a":)" + sid + "}"));  // the designee
  EXPECT_EQ (jobs.GetById (id)->GetStatus (), Job::Status::ACCEPTED);
}

TEST_F (DealTests, LiveBoardRowCarriesNoSettlementMetadata)
{
  /* M2: the settle mode / p / fee-paid keys are stamped only on the history
     snapshot, so a live accepted deal on the board carries none of them.  */
  const auto id = PostAcceptDeal ();
  const Json::Value live = LiveJson (id);
  ASSERT_EQ (live["id"].asUInt64 (), static_cast<Json::UInt64> (id));
  EXPECT_FALSE (live.isMember ("mode"));
  EXPECT_FALSE (live.isMember ("settledp"));
  EXPECT_FALSE (live.isMember ("feepaid"));
}

TEST_F (DealTests, NoArbiterCounterpartyDisputesAfterWorkerConfirm)
{
  /* H1 regression (matrix 1): on a no-arbiter deal a confirmation waives only
     the confirmer's OWN dispute right, so the still-unconfirmed poster remains
     free to dispute the worker's confirm.  With no arbiter that dispute can
     never be ruled, so the sweep settles the blunt 50/50 ghost split -- the
     approved v1 behaviour (a free terminal p=50 no-arbiter dispute).  */
  const auto id = PostAcceptDeal ("");
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_TRUE (Dispute ("poster", id));
  Expire ();
  ExpectNoArbiterGhostSplit (id);
}

TEST_F (DealTests, NoArbiterCounterpartyDisputesAfterPosterConfirm)
{
  /* H1 regression (matrix 2): the mirror image -- the poster confirms and the
     still-unconfirmed worker disputes.  Same free no-arbiter p=50 ghost
     split.  */
  const auto id = PostAcceptDeal ("");
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Dispute ("courier", id));
  Expire ();
  ExpectNoArbiterGhostSplit (id);
}

TEST_F (DealTests, NoArbiterDisputeWithoutConfirmGhostSplits)
{
  /* H1 regression (matrix 3): neither party confirmed, so an unconfirmed party
     disputes straight from the accepted state.  A dispute -- not the untouched
     refund -- is what happened, so the sweep settles the p=50 ghost split, NOT
     the both-stakes refund of the never-touched case.  */
  const auto id = PostAcceptDeal ("");
  EXPECT_TRUE (Dispute ("poster", id));
  Expire ();
  ExpectNoArbiterGhostSplit (id);
}

TEST_F (DealTests, ArbiterGhostsAfterCounterpartyDisputeSplits)
{
  /* H1 regression (matrix 5): the arbiter-bound counterparty-dispute shape --
     the worker confirms, the still-unconfirmed poster disputes, and the bound
     arbiter never rules.  The sweep falls back to the same p=50 split, and
     because the arbiter ghosted the one dispute it was hired to rule its fee
     is FORFEITED: feepaid is stamped false and the arbiter is left untouched
     (worker 4925, poster 4850, treasury 225 burned).  */
  const auto id = PostAcceptDeal ();   // courier2 is the arbiter
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_TRUE (Dispute ("poster", id));
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 4925);
  EXPECT_EQ (Balance ("courier2"), 1000000);   // arbiter forfeited its fee
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50 + 4850);
}

/* -- reaction window pins (design escrow-v1.1 §1) ------------------------- *
   A confirm (leaving a live counter-move) or an arbiter-bound dispute landing
   strictly within the row's reaction_window (W = 30 in regtest) of the deadline
   is no longer terminal: it extends the deadline to now + W so the successor
   move keeps a full window.  A no-arbiter dispute never extends, and an op AT
   the deadline still rejects (JobIsDue).  These flips supersede the v1 no-window
   pins.
   ------------------------------------------------------------------------- */

TEST_F (DealTests, LateConfirmExtendsThenSingleConfirm)
{
  /* A confirm one second before the deadline lands within W and EXTENDS it to
     confirm_ts + W, so the poster keeps a full window.  Silent through the
     extension, the sweep settles the single confirm at p=100 with the SAME
     terminal balances v1 produced (worker 9350, arbiter 500, poster 0).  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + 30);
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9350);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 500);
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50);
}

TEST_F (DealTests, LatePosterConfirmExtendsThenSingleConfirm)
{
  /* The mirror: the POSTER confirms late.  A single confirm settles p=100
     toward the worker regardless of which side confirmed; the extension gives
     the unconfirmed worker its window, whose silence hardens p=100.  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + 30);
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9350);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 500);
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50);
}

TEST_F (DealTests, ConfirmAtDeadlineRejectedRefundsBoth)
{
  /* Unchanged: the end date stays a HARD boundary for ops.  A confirm arriving
     AT the deadline (JobIsDue's exclusive boundary) is rejected, and with
     neither party having acted the sweep refunds both stakes untaxed -- the
     costless miss case, pinned so any change shows in a diff.  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY);
  EXPECT_FALSE (Confirm ("courier", id));
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 50);
  EXPECT_EQ (Balance ("courier2"), 1000000);
}

TEST_F (DealTests, ZeroCollLateConfirmExtendsThenPosterDisputeGhostSplits)
{
  /* Review H1's zero-collateral shape under v1.1: the worker's late confirm no
     longer locks the full reward from the final block -- it EXTENDS, and the
     poster now has a window to dispute.  A no-arbiter dispute does not extend,
     so the sweep settles the p=50 ghost split (worker 2425 on a zero stake,
     poster the mirrored 2425, 150 burned).  */
  const auto id = PostAcceptDeal ("", 0);
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + 30);
  ctx.SetTimestamp (BASE_TS + DAY + 10);         // inside the extension
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + 30);   // no-arbiter: no extend
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 2425);
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50 + 2425);
}

TEST_F (DealTests, ZeroCollLateConfirmExtendsThenSilenceSingleConfirm)
{
  /* The same zero-collateral late confirm, poster silent through the extension:
     it hardens to the v1 single-confirm p=100 (worker 5000 - 150 tax = 4850,
     no arbiter, no stake at risk).  */
  const auto id = PostAcceptDeal ("", 0);
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("courier", id));
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 4850);
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50);
}

TEST_F (DealTests, LateDisputeExtendsThenArbiterRulesInWindow)
{
  /* v1.1 inverts the old "last-block dispute denies the arbiter" pin: an
     arbiter-bound dispute one second before the deadline EXTENDS it, so the
     arbiter keeps a full window -- and its ruling inside the window SUCCEEDS
     (RULING, fee paid, dispute_time stamped).  p=50: worker 4675, arbiter 750,
     poster 4350.  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + 30);
  ctx.SetTimestamp (BASE_TS + DAY + 10);         // inside the extension
  EXPECT_TRUE (Rule ("courier2", id, 50));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 4675);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 750);
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50 + 4350);
}

TEST_F (DealTests, NoArbiterZeroCollateralDisputeTakesHalf)
{
  /* Unchanged: a no-arbiter dispute never extends (its only successor is the
     sweep's 50/50).  The R != C asymmetry pin -- at C=0 the split still moves
     R/2 * (1 - tax) = 2425 to a zero-stake worker, the mirror to the poster,
     150 burned; poster-chosen exposure the client MUST warn about.  */
  const auto id = PostAcceptDeal ("", 0);
  EXPECT_TRUE (Dispute ("courier", id));
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 2425);
  EXPECT_EQ (Balance ("poster"), 1000000 - 5000 - 50 + 2425);
}

/* -- reaction window coverage (design escrow-v1.1 §1/§8) ------------------- */

TEST_F (DealTests, RegtestDefaultReactionWindowIsThirty)
{
  /* The unit/gametest fixtures load the regtest roconfig, whose per-chain
     deal_reaction_window is 30 (mainnet's is 86400); the window tests rely on
     it, and the min(W, d) snapshot stores it on every standard deal.  */
  EXPECT_EQ (ctx.RoConfig ()->params ().deal_reaction_window (), 30);
  EXPECT_EQ (Rwindow (PostAcceptDeal ()), 30);
}

TEST_F (DealTests, ReactionWindowTriggerBoundaryBothSides)
{
  /* STRICT '<': at deadline - now == W nothing is needed (exactly W remains) so
     NO extension; at == W - 1 the extension fires.  */
  const auto a = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 30);         // exactly W left
  EXPECT_TRUE (Confirm ("courier", a));
  EXPECT_EQ (Deadline (a), BASE_TS + DAY);       // unchanged

  ctx.SetTimestamp (BASE_TS);
  const auto b = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 29);         // W - 1 left
  EXPECT_TRUE (Confirm ("courier", b));
  EXPECT_EQ (Deadline (b), BASE_TS + DAY - 29 + 30);
}

TEST_F (DealTests, EarlyConfirmDoesNotShortenDeadline)
{
  /* Monotonic / no-shorten: a confirm far from the deadline would set
     now + W < deadline, so the extension does NOT fire and the deadline is
     never pulled in.  */
  const auto id = PostAcceptDeal ();          // confirm at BASE_TS, W left DAY
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY);
}

TEST_F (DealTests, WorstChainTwoWindowExtensions)
{
  /* The 2W worst chain: a late single confirm (+W) then a late arbiter-bound
     dispute inside that extension (+W) then a ruling inside the second window.
     Total extension stays within 2W of the original deadline.  */
  const auto id = PostAcceptDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + 30);       // +W
  ctx.SetTimestamp (BASE_TS + DAY + 28);                   // inside, 1 left
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY + 28 + 30);      // +W again
  EXPECT_LE (Deadline (id), BASE_TS + DAY + 60);           // <= original + 2W
  ctx.SetTimestamp (BASE_TS + DAY + 57);                   // inside second window
  EXPECT_TRUE (Rule ("courier2", id, 50));
  EXPECT_FALSE (JobExists (id));
}

TEST_F (DealTests, NoArbiterLateConfirmContestableGhostSplits)
{
  /* The pinned policy shift: on a NO-ARBITER deal a late single confirm is now
     contestable for the whole window -- the counterparty's dispute inside the
     extension forces the 50/50 ghost split instead of the confirm locking
     p=100 from the final block.  */
  const auto id = PostAcceptDeal ("");
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + 30);
  ctx.SetTimestamp (BASE_TS + DAY + 5);
  EXPECT_TRUE (Dispute ("poster", id));
  Expire ();
  ExpectNoArbiterGhostSplit (id);
}

TEST_F (DealTests, ZeroWindowRowBehavesLikeV1)
{
  /* A row posted with the window frozen to 0 disables the mechanism: a
     final-block confirm never extends and settles single-confirm p=100 at the
     unmoved deadline -- byte-identical v1 behaviour.  */
  params.Set ("deal-reaction-window", 0);
  const auto id = PostAcceptDeal ();
  EXPECT_EQ (Rwindow (id), 0);
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY);      // no extension
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9350);
}

TEST_F (DealTests, ReactionWindowSnapshotImmuneToRetune)
{
  /* The window is a per-row snapshot: a retune reaches only future posts.  A
     deal posted at W=30 still extends by 30 after the param is set to 0; and a
     deal posted while the param is 0 never extends even after it is raised.  */
  const auto armed = PostAcceptDeal ();          // snapshot W = 30
  params.Set ("deal-reaction-window", 0);
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("courier", armed));
  EXPECT_EQ (Deadline (armed), BASE_TS + DAY - 1 + 30);

  ctx.SetTimestamp (BASE_TS);
  const auto frozen = PostAcceptDeal ();         // param still 0 -> snapshot W = 0
  params.Set ("deal-reaction-window", 30);
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Confirm ("courier", frozen));
  EXPECT_EQ (Deadline (frozen), BASE_TS + DAY);
}

TEST_F (DealTests, LateAcceptThenConfirmExtends)
{
  /* Accept never touches the deadline (design §1): a deal accepted at
     deadline - 1 is near-due, but the worker's confirm IS the window trigger
     and extends it, so a late-accepted deal is protected by the window.  */
  const auto id = PostDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY);      // accept left it untouched
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_EQ (Deadline (id), BASE_TS + DAY - 1 + 30);
  Expire ();
}

TEST_F (DealTests, LateAcceptThenSilenceRefundsBoth)
{
  /* The other late-accept branch: neither party acts after a late accept, so
     the sweep refunds both stakes untaxed (costless, no window needed).  */
  const auto id = PostDeal ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 50);
}

/* -- private deals + dispute_time + JSON (§2/§3) --------------------------- */

TEST_F (DealTests, PrivateDealAtPostRestrictsAccept)
{
  /* A "w" at post makes the deal private from birth: only the designee accepts,
     and the row advertises designated + inviteonly.  */
  CHECK (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":5000,"w":"courier"})"));
  const auto id = LatestJobId ();
  const Json::Value live = LiveJson (id);
  EXPECT_EQ (live["designated"].asString (), "courier");
  EXPECT_TRUE (live["inviteonly"].asBool ());
  EXPECT_FALSE (Process ("green", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
}

TEST_F (DealTests, PrivateDealPostRejectsBadWorker)
{
  /* A non-empty "w" must be an existing initialised account, != poster, !=
     arbiter, and a string -- any violation rejects the WHOLE post uncharged.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"w":"poster"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"arbiter":"courier2","fee":0,"w":"courier2"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"w":"ghost"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":0,"w":5})"));
  EXPECT_EQ (jobs.CountAll (), 0);
}

TEST_F (DealTests, PrivateUnassignedInviteOnly)
{
  /* w:"" is the private-unassigned state: invite-only with no designee, so
     NOBODY can accept until ASSIGN names one -- then only that designee can.  */
  CHECK (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":5000,"w":""})"));
  const auto id = LatestJobId ();
  const std::string sid = std::to_string (id);
  EXPECT_TRUE (LiveJson (id)["inviteonly"].asBool ());
  EXPECT_FALSE (LiveJson (id).isMember ("designated"));
  EXPECT_FALSE (Process ("courier", R"({"a":)" + sid + "}"));   // nobody yet
  EXPECT_TRUE (Process ("poster", R"({"s":)" + sid + R"(,"w":"courier"})"));
  EXPECT_TRUE (LiveJson (id)["inviteonly"].asBool ());          // persists
  EXPECT_FALSE (Process ("green", R"({"a":)" + sid + "}"));     // not designee
  EXPECT_TRUE (Process ("courier", R"({"a":)" + sid + "}"));
}

TEST_F (DealTests, AssignArbiterRejected)
{
  /* F7: assigning the bound arbiter as worker is rejected (it would strand the
     row -- the accept gate bars an arbiter-worker); a non-arbiter assigns.  */
  const auto id = PostDeal ();                   // arbiter courier2
  const std::string sid = std::to_string (id);
  EXPECT_FALSE (Process ("poster", R"({"s":)" + sid + R"(,"w":"courier2"})"));
  EXPECT_TRUE (Process ("poster", R"({"s":)" + sid + R"(,"w":"courier"})"));
}

TEST_F (DealTests, DisputeTimeAndWindowInJson)
{
  /* reactionwindow rides on the live row; disputetime appears only after a
     dispute, on both the live row and the settled history snapshot.  */
  const auto id = PostAcceptDeal ();
  EXPECT_EQ (LiveJson (id)["reactionwindow"].asInt64 (), 30);
  EXPECT_FALSE (LiveJson (id).isMember ("disputetime"));
  ctx.SetTimestamp (BASE_TS + 100);
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_EQ (LiveJson (id)["disputetime"].asInt64 (), BASE_TS + 100);
  EXPECT_TRUE (Rule ("courier2", id, 50));
}

/* -- admission-cap clamps + retention overlay (§4/§5/§6) ------------------- */

TEST_F (DealTests, ReactionWindowClampedAtPost)
{
  /* An over-ceiling deal-reaction-window override clamps to CAP (30 days) in
     the snapshot exactly as getjobsparams reports it -- RPC == consensus.  */
  params.Set ("deal-reaction-window", CAP_DEAL_REACTION_WINDOW + 1000);
  CHECK (Process ("poster",
      R"({"t":"deal","d":2592000,"r":5000,"co":0,"terms":"long"})"));
  const auto id = LatestJobId ();
  EXPECT_EQ (Rwindow (id), CAP_DEAL_REACTION_WINDOW);
  GameStateJson gsj(db, ctx);
  EXPECT_EQ (gsj.JobsParams ()["deal-reaction-window"].asInt64 (),
             CAP_DEAL_REACTION_WINDOW);
}

TEST_F (DealTests, JobsParamsClampsOverridesToCeilingsAndFloors)
{
  /* getjobsparams returns the POST-CLAMP effective value consensus uses: a
     ceiling caps an over-ceiling override and a negative override floors
     to 0.  */
  params.Set ("max-live-jobs", CAP_MAX_LIVE_JOBS + 5);
  params.Set ("max-jobs-per-poster", -7);
  GameStateJson gsj(db, ctx);
  const Json::Value p = gsj.JobsParams ();
  EXPECT_EQ (p["max-live-jobs"].asInt64 (), CAP_MAX_LIVE_JOBS);
  EXPECT_EQ (p["max-jobs-per-poster"].asInt64 (), 0);
  /* A self-bounding param reports the raw overlay (no clamp).  */
  params.Set ("deal-tax-bps", 4321);
  EXPECT_EQ (gsj.JobsParams ()["deal-tax-bps"].asInt64 (), 4321);
}

TEST_F (DealTests, ProBonoArbiterSettlesWithZeroFee)
{
  /* L4: a pro-bono arbiter (bound, fee_bps 0) carries the deal to a
     both-confirm settle and receives nothing -- the fee schedule is honoured
     by moving zero coins, and the parties are paid as if no arbiter existed. */
  CHECK (Process ("poster",
      R"({"t":"deal","d":86400,"r":5000,"co":5000,"arbiter":"courier2"})"));
  const auto id = LatestJobId ();
  CHECK (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_FALSE (JobExists (id));
  /* p=100, fee 0: worker <- 5000 - 150(tax) + 5000(collateral) = 9850; the
     arbiter is bound but paid nothing.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5000 + 9850);
  EXPECT_EQ (Balance ("courier2"), 1000000);   // pro-bono: zero coins move
}

TEST_F (DealTests, OpenDealExpiresRefundingThePoster)
{
  /* L6a: an OPEN deal that is never accepted expires through the void hook,
     which refunds the poster's reward and touches no deal proto -- the deal
     settlement path (RefundBothDeal / SettleDeal) never runs.  */
  const auto id = PostDeal ();   // arbiter named, but never accepted
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 50);   // reward refunded, fee burned
}

} // anonymous namespace
} // namespace pxd
