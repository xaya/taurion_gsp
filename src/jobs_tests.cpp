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
/** The configured bounty cancel notice (7 days).  */
constexpr int64_t NOTICE = 7 * DAY;

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
       100, 1000 and 1000 vCHI); the tests use small rewards throughout, so
       lower them exactly as an admin would.  The roconfig-default fallback
       itself is exercised end-to-end by jobs_caps.py.  */
    params.Set ("min-job-reward", 1);
    params.Set ("min-bounty-reward", 1);
    params.Set ("min-deal-reward", 1);
  }

  JobContext
  Ctx ()
  {
    return {ctx, accounts, buildings, jobs, params};
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

  /** Creates an undocked character at the given map position.  */
  Database::IdT
  MakeCharacterAt (const std::string& owner, const HexCoord& pos)
  {
    auto c = characters.CreateNew (owner, accounts.GetByName (owner)
                                              ->GetFaction ());
    const auto id = c->GetId ();
    c->SetPosition (pos);
    return id;
  }

  /**
   * Simulates the pre-removal kill attribution for one character death with
   * the given attacker characters, exactly as combat wires it up.
   */
  void
  KillWithAttackers (const Database::IdT victim,
                     const std::vector<Database::IdT>& attackers)
  {
    DamageLists dl(db, ctx.Height ());
    for (const auto a : attackers)
      dl.AddEntry (victim, a);

    JobsBountyTracker tracker(db, ctx, dl);
    proto::TargetId target;
    target.set_type (proto::TargetId::TYPE_CHARACTER);
    target.set_id (victim);
    tracker.UpdateForKill (target);
  }

  /** Returns (completed, value) for an account.  */
  std::tuple<unsigned, Amount>
  JobStats (const std::string& name)
  {
    const auto& pb = accounts.GetByName (name)->GetProto ();
    return {pb.jobs_completed (), pb.jobs_value_completed ()};
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
class WantedTests : public JobsTests
{

protected:

  /** Posts a standing bounty on green: r=9000, n=3 (tranche 3000).  */
  Database::IdT
  PostBounty ()
  {
    CHECK (Process ("poster",
        R"({"t":"wanted","r":9000,"co":0,"name":"green","n":3})"));
    return OnlyJobId ();
  }

};

TEST_F (WantedTests, PostHappy)
{
  const auto id = PostBounty ();
  auto j = jobs.GetById (id);
  ASSERT_NE (j, nullptr);
  EXPECT_FALSE (j->HasDeadline ());
  EXPECT_EQ (j->GetFaction (), Faction::INVALID);
  EXPECT_EQ (j->GetLinkedName (), "green");
  EXPECT_EQ (j->GetProto ().wanted ().quota (), 3);
  EXPECT_EQ (j->GetProto ().wanted ().remaining (), 3);
  EXPECT_EQ (j->GetProto ().wanted ().tranche (), 3000);
  /* r 9000 + fee max(1, 9000*1%) = 90.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 9000 - 90);
}

TEST_F (WantedTests, PostRejects)
{
  /* Unknown target; self-bounty; bad quota; nonzero collateral; a reward too
     small for the quota (zero tranche).  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":0,"name":"ghost","n":3})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":0,"name":"poster","n":3})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":0,"name":"green","n":0})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":0,"name":"green","n":26})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":5,"name":"green","n":3})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":2,"co":0,"name":"green","n":3})"));
  /* Integral-real quota (strict-integer convention).  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":0,"name":"green","n":3.0})"));
}

TEST_F (WantedTests, NoAccept)
{
  const auto id = PostBounty ();
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  /* The generic fulfil op was removed with the delivery types; the grammar
     no longer parses it at all.  */
  EXPECT_FALSE (ParseOk (R"({"f":)" + std::to_string (id) + "}"));
}

TEST_F (WantedTests, KillPaysOneTranche)
{
  const auto id = PostBounty ();
  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));
  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));

  KillWithAttackers (victim, {hunter});

  auto j = jobs.GetById (id);
  ASSERT_NE (j, nullptr);
  EXPECT_EQ (j->GetProto ().wanted ().remaining (), 2);
  EXPECT_EQ (j->GetReward (), 6000);
  EXPECT_EQ (Balance ("courier"), 1000000 + 3000);
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (1u, 3000));
}

TEST_F (WantedTests, ZeroShareBurnsTrancheWithoutReputation)
{
  /* Reward 3 over quota 3 => tranche 1.  Two distinct killers cannot split
     one coin, so nobody is paid; the tranche is burned and -- unlike before
     the fix -- NO completion reputation is credited.  */
  CHECK (Process ("poster",
      R"({"t":"wanted","r":3,"co":0,"name":"green","n":3})"));
  const auto id = OnlyJobId ();
  const auto reward0 = jobs.GetById (id)->GetReward ();

  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));
  const auto h1 = MakeCharacterAt ("courier", HexCoord (6, 5));
  const auto h2 = MakeCharacterAt ("courier2", HexCoord (7, 5));

  KillWithAttackers (victim, {h1, h2});

  auto j = jobs.GetById (id);
  ASSERT_NE (j, nullptr);
  /* The tranche is consumed: remaining decremented, reward reduced by one.  */
  EXPECT_EQ (j->GetProto ().wanted ().remaining (), 2);
  EXPECT_EQ (j->GetReward (), reward0 - 1);
  /* Nobody paid, nobody's completion counter bumped.  */
  EXPECT_EQ (Balance ("courier"), 1000000);
  EXPECT_EQ (Balance ("courier2"), 1000000);
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (0u, 0));
  EXPECT_EQ (JobStats ("courier2"), std::make_tuple (0u, 0));
}

TEST_F (WantedTests, KillSplitsAcrossDistinctOwners)
{
  PostBounty ();
  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));
  /* Two of courier's characters and one of courier2's tag the kill: the
     tranche splits per distinct OWNER (1500 each), not per character.  */
  const auto h1 = MakeCharacterAt ("courier", HexCoord (6, 5));
  const auto h2 = MakeCharacterAt ("courier", HexCoord (7, 5));
  const auto h3 = MakeCharacterAt ("courier2", HexCoord (8, 5));

  KillWithAttackers (victim, {h1, h2, h3});

  EXPECT_EQ (Balance ("courier"), 1000000 + 1500);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 1500);
}

TEST_F (WantedTests, MenteconSelfTagPaysNothing)
{
  /* H1: the target's OWN character lands on the victim's damage list via
     mentecon friendly-fire.  With no hostile hunter present, the friendly
     owner is filtered before the divisor, the eligible set is empty, and the
     kill pays nobody and consumes nothing -- the target cannot claw back the
     bounty posted against it.  */
  const auto id = PostBounty ();
  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));
  const auto selfTag = MakeCharacterAt ("green", HexCoord (6, 5));

  KillWithAttackers (victim, {selfTag});

  auto j = jobs.GetById (id);
  ASSERT_NE (j, nullptr);
  EXPECT_EQ (j->GetProto ().wanted ().remaining (), 3);
  EXPECT_EQ (j->GetReward (), 9000);
  EXPECT_EQ (Balance ("green"), 1000000);
  EXPECT_EQ (JobStats ("green"), std::make_tuple (0u, 0));
}

TEST_F (WantedTests, MenteconFriendlyTagsDoNotDiluteHunter)
{
  /* H1: a hostile hunter kills the victim while the target's own AND an allied
     (same-faction) character also tag the kill via mentecon.  Both friendly
     owners are filtered, so the hunter takes the WHOLE tranche -- not a
     diluted share -- and neither friendly is credited.  */
  MakeAccount ("greenally", Faction::GREEN, 1000000);
  const auto id = PostBounty ();
  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));
  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));
  const auto selfTag = MakeCharacterAt ("green", HexCoord (7, 5));
  const auto allyTag = MakeCharacterAt ("greenally", HexCoord (8, 5));

  KillWithAttackers (victim, {hunter, selfTag, allyTag});

  EXPECT_EQ (jobs.GetById (id)->GetProto ().wanted ().remaining (), 2);
  EXPECT_EQ (Balance ("courier"), 1000000 + 3000);
  EXPECT_EQ (Balance ("green"), 1000000);
  EXPECT_EQ (Balance ("greenally"), 1000000);
  EXPECT_EQ (JobStats ("green"), std::make_tuple (0u, 0));
  EXPECT_EQ (JobStats ("greenally"), std::make_tuple (0u, 0));
}

TEST_F (WantedTests, FedCharacterCountsAsTargetKill)
{
  /* Pinned product decision (v22 M2): the target is matched by CURRENT owner
     at death.  A same-faction ally can transfer a throwaway character onto the
     bounty target; a hostile hunter's kill then counts as the target's death
     and pays the hunter.  Accepted "current-owner" semantics -- feeding a
     third-party poster's escrow is the one case worth closing before a real
     economy (a transfer-into-a-live-target guard), tracked for pre-launch.  */
  MakeAccount ("greenfeeder", Faction::GREEN, 1000000);
  const auto id = PostBounty ();
  const auto fed = MakeCharacterAt ("greenfeeder", HexCoord (5, 5));
  { auto c = characters.GetById (fed); c->SetOwner ("green"); }
  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));

  KillWithAttackers (fed, {hunter});

  EXPECT_EQ (jobs.GetById (id)->GetProto ().wanted ().remaining (), 2);
  EXPECT_EQ (Balance ("courier"), 1000000 + 3000);
}

TEST_F (WantedTests, EvadedCharacterEscapesTheBounty)
{
  /* Pinned product decision (v22 M2, evasion): current-owner matching means
     the target can hand a doomed character to a same-faction alt before death,
     and the bounty on the ORIGINAL target does not see the kill.  Accepted
     low-severity dodge (the poster is refunded any unearned reward on
     expiry).  */
  MakeAccount ("greenalt", Faction::GREEN, 1000000);
  const auto id = PostBounty ();
  const auto doomed = MakeCharacterAt ("green", HexCoord (5, 5));
  { auto c = characters.GetById (doomed); c->SetOwner ("greenalt"); }
  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));

  KillWithAttackers (doomed, {hunter});

  EXPECT_EQ (jobs.GetById (id)->GetProto ().wanted ().remaining (), 3);
  EXPECT_EQ (Balance ("courier"), 1000000);
}

TEST_F (WantedTests, AggregatedPayoutAcrossStackedPools)
{
  /* Three pools with DIFFERENT tranches on one target, two distinct killer
     owners (one of them tagging with two characters): every pool's share
     accrues, but each account is opened and credited ONCE, with its
     completion counter bumped once per PAYING pool.  Expected per owner:
     60/2 quota 2 -> tranche 30 -> 15; 100/1 -> 100 -> 50; 45/5 -> 9 -> 4;
     total 69 across three paying pools.  */
  CHECK (Process ("poster",
      R"({"t":"wanted","r":60,"co":0,"name":"green","n":2})"));
  CHECK (Process ("poster",
      R"({"t":"wanted","r":100,"co":0,"name":"green","n":1})"));
  CHECK (Process ("poster",
      R"({"t":"wanted","r":45,"co":0,"name":"green","n":5})"));

  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));
  const auto h1a = MakeCharacterAt ("courier", HexCoord (6, 5));
  const auto h1b = MakeCharacterAt ("courier", HexCoord (7, 5));
  const auto h2 = MakeCharacterAt ("courier2", HexCoord (8, 5));

  KillWithAttackers (victim, {h1a, h1b, h2});

  EXPECT_EQ (Balance ("courier"), 1000000 + 69);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 69);
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (3u, 69));
  EXPECT_EQ (JobStats ("courier2"), std::make_tuple (3u, 69));

  /* The single-kill pool drained and left the board; the others consumed
     one tranche each.  */
  std::map<Amount, unsigned> remaining;
  auto res = jobs.QueryAll ();
  while (res.Step ())
    {
      auto j = jobs.GetFromResult (res);
      remaining[j->GetProto ().wanted ().tranche ()]
          = j->GetProto ().wanted ().remaining ();
    }
  EXPECT_EQ (remaining,
             (std::map<Amount, unsigned> {{30, 1}, {9, 4}}));
}

TEST_F (WantedTests, AggregatedPayoutSkipsZeroSharePools)
{
  /* A zero-share pool mixed with a paying one: the zero-share tranche is
     burned without credit (the N-1 rule), while the paying pool's share
     lands -- so the aggregate credit is one completion of 5, not two.  */
  CHECK (Process ("poster",
      R"({"t":"wanted","r":1,"co":0,"name":"green","n":1})"));
  CHECK (Process ("poster",
      R"({"t":"wanted","r":10,"co":0,"name":"green","n":1})"));

  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));
  const auto h1 = MakeCharacterAt ("courier", HexCoord (6, 5));
  const auto h2 = MakeCharacterAt ("courier2", HexCoord (7, 5));

  KillWithAttackers (victim, {h1, h2});

  EXPECT_EQ (Balance ("courier"), 1000000 + 5);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 5);
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (1u, 5));
  EXPECT_EQ (JobStats ("courier2"), std::make_tuple (1u, 5));
  /* Both single-kill pools are consumed off the board either way.  */
  EXPECT_EQ (jobs.CountAll (), 0);
}

TEST_F (WantedTests, TurretOnlyKillPaysNothing)
{
  const auto id = PostBounty ();
  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));

  /* No damage-list entries (e.g. only building attacks): pays nothing,
     consumes nothing.  */
  KillWithAttackers (victim, {});

  EXPECT_EQ (jobs.GetById (id)->GetProto ().wanted ().remaining (), 3);
  EXPECT_EQ (Balance ("courier"), 1000000);
}

TEST_F (WantedTests, UnrelatedDeathIsNoOp)
{
  const auto id = PostBounty ();
  /* courier2 is not under bounty; their death must not touch the pool.  */
  const auto victim = MakeCharacterAt ("courier2", HexCoord (5, 5));
  const auto hunter = MakeCharacterAt ("green", HexCoord (6, 5));
  KillWithAttackers (victim, {hunter});
  EXPECT_EQ (jobs.GetById (id)->GetProto ().wanted ().remaining (), 3);
}

TEST_F (WantedTests, SameBlockKillsBeyondPoolDoNotCrash)
{
  /* Production constructs ONE tracker per block: if the target loses more
     characters in a block than the pool has tranches, the later kills find
     the pool already drained -- the indexed re-probe comes back empty, the
     owner is memoised, and the kill must be a graceful no-op (this was a
     CHECK-abort -- a chain halt -- before the fix).  */
  ASSERT_TRUE (Process ("poster",
      R"({"t":"wanted","r":3000,"co":0,"name":"green","n":1})"));
  const auto id = OnlyJobId ();

  const auto v1 = MakeCharacterAt ("green", HexCoord (5, 5));
  const auto v2 = MakeCharacterAt ("green", HexCoord (5, 6));
  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));

  DamageLists dl(db, ctx.Height ());
  dl.AddEntry (v1, hunter);
  dl.AddEntry (v2, hunter);

  JobsBountyTracker tracker(db, ctx, dl);
  proto::TargetId target;
  target.set_type (proto::TargetId::TYPE_CHARACTER);

  target.set_id (v1);
  tracker.UpdateForKill (target);   // drains and deletes the pool
  EXPECT_FALSE (JobExists (id));

  target.set_id (v2);
  tracker.UpdateForKill (target);   // stale set entry: must no-op

  EXPECT_EQ (Balance ("courier"), 1000000 + 3000);
}

TEST_F (WantedTests, PoolDrainsAndBurnsDust)
{
  /* r=9001, n=2 -> tranche 4500, dust 1.  */
  ASSERT_TRUE (Process ("poster",
      R"({"t":"wanted","r":9001,"co":0,"name":"green","n":2})"));
  const auto id = OnlyJobId ();

  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));
  KillWithAttackers (MakeCharacterAt ("green", HexCoord (5, 5)), {hunter});
  EXPECT_TRUE (JobExists (id));
  KillWithAttackers (MakeCharacterAt ("green", HexCoord (5, 6)), {hunter});

  /* Pool complete: row gone, hunter has both tranches, the dust (1) burned.  */
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 9000);
}

TEST_F (WantedTests, StackedPoolsBothPay)
{
  PostBounty ();
  ASSERT_TRUE (Process ("courier2",
      R"({"t":"wanted","r":3000,"co":0,"name":"green","n":3})"));

  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));
  KillWithAttackers (MakeCharacterAt ("green", HexCoord (5, 5)), {hunter});

  /* One kill pays a tranche from BOTH pools on the name: 3000 + 1000.  */
  EXPECT_EQ (Balance ("courier"), 1000000 + 3000 + 1000);
}

TEST_F (WantedTests, NoticeCancelLifecycle)
{
  const auto id = PostBounty ();
  const std::string c = R"({"c":)" + std::to_string (id) + "}";

  /* Notice: the job stays, now with a deadline notice out.  */
  ASSERT_TRUE (Process ("poster", c));
  {
    auto j = jobs.GetById (id);
    ASSERT_NE (j, nullptr);
    ASSERT_TRUE (j->HasDeadline ());
    EXPECT_EQ (j->GetDeadline (), BASE_TS + NOTICE);
  }

  /* No window-pushing: a second cancel is rejected.  */
  EXPECT_FALSE (Process ("poster", c));

  /* A kill during the notice window still pays.  */
  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));
  ctx.SetTimestamp (BASE_TS + NOTICE - 10);
  KillWithAttackers (MakeCharacterAt ("green", HexCoord (5, 5)), {hunter});
  EXPECT_EQ (Balance ("courier"), 1000000 + 3000);

  /* When the notice expires, only the unearned remainder refunds.  */
  ctx.SetTimestamp (BASE_TS + NOTICE + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 90 - 3000);
}

TEST_F (WantedTests, SameBlockKillBeatsNoticeExpiry)
{
  /* A qualifying kill in the very block the notice expires still pays (kills
     process before the expiry sweep).  */
  const auto id = PostBounty ();
  ASSERT_TRUE (Process ("poster", R"({"c":)" + std::to_string (id) + "}"));

  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));
  ctx.SetTimestamp (BASE_TS + NOTICE + 1);
  KillWithAttackers (MakeCharacterAt ("green", HexCoord (5, 5)), {hunter});
  ExpireJobs (db, ctx);

  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 3000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 90 - 3000);
}

/* ************************************************************************** */
/* Ad-slot rentals.                                                           */

class AdTests : public JobsTests
{

protected:

  /** courier rents ad slot 2 on poster's building 1 for 500.  */
  Database::IdT
  PostAd ()
  {
    CHECK (Process ("courier",
        R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,)"
        R"("hash":"abc123"})"));
    return OnlyJobId ();
  }

};

TEST_F (AdTests, LifecycleSuccess)
{
  const auto id = PostAd ();
  /* The building owner is auto-designated; their accept is the approval.  */
  EXPECT_EQ (jobs.GetById (id)->GetProto ().designated_worker (), "poster");
  EXPECT_EQ (jobs.GetById (id)->GetFaction (), Faction::INVALID);
  ASSERT_TRUE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));

  /* The period elapses: the rent pays the owner.  */
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  /* fee = max(1, 500*1%) = 5.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 500 - 5);
  EXPECT_EQ (Balance ("poster"), 1000000 + 500);
}

TEST_F (AdTests, PostRejects)
{
  /* An ad rents a calendar window and takes no work window; the removed "wd"
     key is no longer in the grammar, so a post carrying it does not parse.  */
  EXPECT_FALSE (ParseOk (
      R"({"t":"ad","d":86400,"wd":86400,"r":500,"co":0,"b":1,"slot":2,"hash":"abc"})"));
  /* Own building; ancient building; empty hash; nonzero collateral.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,"hash":"abc"})"));
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":2,"slot":2,"hash":"abc"})"));
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,"hash":""})"));
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":5,"b":1,"slot":2,"hash":"abc"})"));
}

TEST_F (AdTests, OwnershipRevalidatedAtAccept)
{
  const auto id = PostAd ();
  /* The building changes hands after the post: the (still-designated) old
     owner may no longer accept.  */
  buildings.GetById (1)->SetOwner ("courier2");
  EXPECT_FALSE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));
}

TEST_F (AdTests, BuildingDestroyedRefundsAdvertiser)
{
  const auto id = PostAd ();
  ASSERT_TRUE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));
  OnJobEntityDestroyed (db, ctx, 1);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5);
  EXPECT_EQ (Balance ("poster"), 1000000);
}

TEST_F (AdTests, ScheduledLifecycle)
{
  /* The slot is rented for the second day of a two-day term; the accept
     happens before the window opens, the payout at the deadline as usual.  */
  CHECK (Process ("courier",
      R"({"t":"ad","d":172800,"r":500,"co":0,"b":1,"slot":2,)"
      R"("hash":"abc123","start":86400})"));
  const auto id = OnlyJobId ();
  EXPECT_EQ (jobs.GetById (id)->GetProto ().ad ().start (), BASE_TS + DAY);
  ASSERT_TRUE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));

  ctx.SetTimestamp (BASE_TS + 2 * DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 500 - 5);
  EXPECT_EQ (Balance ("poster"), 1000000 + 500);
}

TEST_F (AdTests, PostRejectsBadSchedule)
{
  /* Negative / non-integer start; a rented window shorter than the duration
     floor (a window exactly at the floor is fine).  */
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,)"
      R"("hash":"abc","start":-1})"));
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,)"
      R"("hash":"abc","start":"soon"})"));
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,)"
      R"("hash":"abc","start":83000})"));
  /* Integral-real start / slot (strict-integer convention).  */
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,)"
      R"("hash":"abc","start":3600.0})"));
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2.0,)"
      R"("hash":"abc"})"));
  EXPECT_TRUE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,)"
      R"("hash":"abc","start":82800})"));
}

TEST_F (AdTests, OverlappingAcceptRejected)
{
  const auto first = PostAd ();
  ASSERT_TRUE (Process ("poster", R"({"a":)" + std::to_string (first) + "}"));

  /* Same slot, overlapping window: the accept is the booking, so the second
     one is rejected -- both a full overlap and a partially overlapping
     scheduled window.  */
  CHECK (Process ("courier2",
      R"({"t":"ad","d":86400,"r":300,"co":0,"b":1,"slot":2,"hash":"def"})"));
  const auto full = LatestJobId ();
  EXPECT_FALSE (Process ("poster", R"({"a":)" + std::to_string (full) + "}"));

  CHECK (Process ("courier2",
      R"({"t":"ad","d":172800,"r":300,"co":0,"b":1,"slot":2,)"
      R"("hash":"def","start":43200})"));
  const auto partial = LatestJobId ();
  EXPECT_FALSE (Process ("poster",
      R"({"a":)" + std::to_string (partial) + "}"));

  /* A different slot on the same building is free to let.  */
  CHECK (Process ("courier2",
      R"({"t":"ad","d":86400,"r":300,"co":0,"b":1,"slot":3,"hash":"def"})"));
  EXPECT_TRUE (Process ("poster",
      R"({"a":)" + std::to_string (LatestJobId ()) + "}"));
}

TEST_F (AdTests, BackToBackWindowsShareTheSlot)
{
  const auto first = PostAd ();
  ASSERT_TRUE (Process ("poster", R"({"a":)" + std::to_string (first) + "}"));

  /* The next window starts exactly at the first one's deadline: the deadline
     is exclusive, so this is a legal back-to-back booking.  */
  CHECK (Process ("courier2",
      R"({"t":"ad","d":172800,"r":300,"co":0,"b":1,"slot":2,)"
      R"("hash":"def","start":86400})"));
  EXPECT_TRUE (Process ("poster",
      R"({"a":)" + std::to_string (LatestJobId ()) + "}"));
}

TEST_F (AdTests, AcceptKeepsCalendarDeadline)
{
  /* An ad rents a calendar window: a late accept (approval) must NOT move
     the deadline (the deadline IS the window end).  */
  const auto id = PostAd ();
  ctx.SetTimestamp (BASE_TS + 3600);
  ASSERT_TRUE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_EQ (jobs.GetById (id)->GetDeadline (), BASE_TS + DAY);
}

TEST_F (AdTests, AcceptRequiresMinimumWindowLeft)
{
  /* The payout is always the full rent, so the owner's approval must leave
     at least min_ad_window of displayable time.  Exactly the floor is
     accepted (inclusive); less is rejected, and the unapproved ad then
     voids at its deadline with a refund.  */
  const auto first = PostAd ();
  ctx.SetTimestamp (BASE_TS + DAY - 1800);
  EXPECT_FALSE (Process ("poster", R"({"a":)" + std::to_string (first) + "}"));

  ctx.SetTimestamp (BASE_TS);
  CHECK (Process ("courier2",
      R"({"t":"ad","d":86400,"r":300,"co":0,"b":1,"slot":3,"hash":"def"})"));
  const auto second = LatestJobId ();
  ctx.SetTimestamp (BASE_TS + DAY - 3600);
  EXPECT_TRUE (Process ("poster", R"({"a":)" + std::to_string (second) + "}"));
}

TEST_F (AdTests, AcceptRejectsElapsedWindow)
{
  /* An ACCEPTED ad pays its full rent at the sweep: accepting one whose
     calendar window already elapsed would hand the owner the rent for zero
     display time, so a due ad is the sweep's to void (exclusive boundary,
     like every accept).  */
  const auto id = PostAd ();
  ctx.SetTimestamp (BASE_TS + DAY);
  EXPECT_FALSE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));
  ctx.SetTimestamp (BASE_TS + DAY + 50);
  EXPECT_FALSE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 - 5);
  EXPECT_EQ (Balance ("poster"), 1000000);
}

TEST_F (AdTests, DueCompetitorDoesNotBlockAccept)
{
  /* The first ad's window has fully elapsed but this block's expiry sweep
     has not run yet: it no longer blocks the slot (moves run before
     ExpireJobs, mirroring the due-job accept rule).  */
  CHECK (Process ("courier",
      R"({"t":"ad","d":7200,"r":500,"co":0,"b":1,"slot":2,"hash":"abc"})"));
  const auto first = OnlyJobId ();
  ASSERT_TRUE (Process ("poster", R"({"a":)" + std::to_string (first) + "}"));

  CHECK (Process ("courier2",
      R"({"t":"ad","d":86400,"r":300,"co":0,"b":1,"slot":2,"hash":"def"})"));
  const auto second = LatestJobId ();

  ctx.SetTimestamp (BASE_TS + 7200);
  EXPECT_TRUE (Process ("poster", R"({"a":)" + std::to_string (second) + "}"));
}

TEST_F (AdTests, BuildingSoldVoidsAdsOnly)
{
  /* An accepted and an open ad both void with a full refund when the
     building is sold; a job not linked to the building (a deal) is
     unaffected.  */
  const auto accepted = PostAd ();
  ASSERT_TRUE (Process ("poster",
      R"({"a":)" + std::to_string (accepted) + "}"));
  CHECK (Process ("courier2",
      R"({"t":"ad","d":172800,"r":300,"co":0,"b":1,"slot":2,)"
      R"("hash":"def","start":86400})"));
  const auto open = LatestJobId ();
  CHECK (Process ("courier2",
      R"({"t":"deal","d":86400,"r":2000,"co":0,"terms":"x"})"));
  const auto bystander = LatestJobId ();

  buildings.GetById (1)->SetOwner ("courier2");
  OnJobBuildingTransferred (db, ctx, 1);

  EXPECT_FALSE (JobExists (accepted));
  EXPECT_FALSE (JobExists (open));
  EXPECT_TRUE (JobExists (bystander));
  /* The advertisers get their rent back (posting fees are burned); the old
     owner is not paid, and the bystander deal's reward stays escrowed.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5);
  EXPECT_EQ (Balance ("courier2"), 1000000 - 3 - 2000 - 20);
  EXPECT_EQ (Balance ("poster"), 1000000);
}

/* ************************************************************************** */
/* Escrow deals.                                                              */

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

  /** Returns (arbiter_rulings, arbiter_ghosted) for an account.  */
  std::pair<unsigned, unsigned>
  ArbiterStats (const std::string& name)
  {
    const auto& pb = accounts.GetByName (name)->GetProto ();
    return {pb.arbiter_rulings (), pb.arbiter_ghosted ()};
  }

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

  /**
   * Renders the settled-history JSON row for a job id (a null value if the id
   * is not in history).  The settlement metadata (mode / settledp / feepaid)
   * is stamped only here, so it is exercised through the same JSON the clients
   * read.
   */
  Json::Value
  HistoryJson (const Database::IdT id)
  {
    GameStateJson gsj(db, ctx);
    const Json::Value arr = gsj.JobsHistory (0, 0, 0, 0);
    for (const auto& e : arr)
      if (e["id"].asUInt64 () == static_cast<Json::UInt64> (id))
        return e;
    return Json::Value ();
  }

  /** Renders the live board JSON row for a job id (null if it is not live).  */
  Json::Value
  LiveJson (const Database::IdT id)
  {
    GameStateJson gsj(db, ctx);
    const Json::Value arr = gsj.JobsPage (0, JobsTable::MAX_PAGE);
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
    const Json::Value h = HistoryJson (id);
    EXPECT_EQ (h["mode"].asString (), "ghost-split");
    EXPECT_EQ (h["settledp"].asUInt (), 50u);
    EXPECT_FALSE (h.isMember ("feepaid"));   // no arbiter bound
    /* THE trap in the arbiter record: GHOST_SPLIT is also how a deal that
       never named an arbiter settles a dispute, so no arbiter counter may move
       here.  courier2 (the arbiter of every OTHER deal in this fixture) is a
       bystander to these and must stay untouched -- otherwise a ghosting
       penalty would land on whoever happens to arbitrate elsewhere.  Both
       parties still carry the dispute itself.  */
    EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (0u, 0u));
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
  { return jobs.GetById (id)->GetProto ().deal ().reaction_window (); }

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
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (0u, 0u));
  /* The history snapshot records how the deal settled.  */
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["outcome"].asString (), "completed");
  EXPECT_EQ (h["mode"].asString (), "both-confirm");
  EXPECT_EQ (h["settledp"].asUInt (), 100u);
  EXPECT_TRUE (h["feepaid"].asBool ());
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
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (1u, 0u));
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["outcome"].asString (), "completed");
  EXPECT_EQ (h["mode"].asString (), "ruling");
  EXPECT_EQ (h["settledp"].asUInt (), 30u);
  EXPECT_TRUE (h["feepaid"].asBool ());
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "ghost-split");
  EXPECT_EQ (h["settledp"].asUInt (), 50u);
  EXPECT_FALSE (h["feepaid"].asBool ());   // the arbiter forfeited its fee
  /* L3: a ghost split is a tax-bearing settlement with p>0, so it DOES bump
     the worker's deal record -- deals_completed counts these (not just clean
     completions), and the value tracks the earned reward share R*50/100 = 2500,
     not the full 5000 reward.  Phase-2 reputation must read the counter this
     way.  */
  EXPECT_EQ (DealStats ("courier"),
             std::make_pair (1u, static_cast<Amount> (2500)));
  /* The poster's mirror follows the same p; and THIS is the arbiter ghost --
     bound to the dispute, never ruled it -- recorded as such rather than being
     inferred from a missing ruling.  */
  EXPECT_EQ (PosterStats ("poster"), std::make_pair (1u, static_cast<Amount> (2500)));
  EXPECT_EQ (Disputed ("poster"), 1u);
  EXPECT_EQ (Disputed ("courier"), 1u);
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (0u, 1u));
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
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (0u, 0u));
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "single-confirm");
  EXPECT_EQ (h["settledp"].asUInt (), 100u);
  EXPECT_TRUE (h["feepaid"].asBool ());
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
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (0u, 0u));
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["outcome"].asString (), "void");
  EXPECT_EQ (h["mode"].asString (), "refund");
  EXPECT_FALSE (h.isMember ("settledp"));   // nothing transacted
  EXPECT_FALSE (h["feepaid"].asBool ());    // arbiter bound but unpaid
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["outcome"].asString (), "void");
  EXPECT_EQ (h["mode"].asString (), "refund");
  EXPECT_FALSE (h.isMember ("settledp"));
  EXPECT_FALSE (h.isMember ("feepaid"));
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
  EXPECT_EQ (ArbiterStats ("courier2"), std::make_pair (1u, 0u));
  /* A p=0 ruling still records the actual ruling (settledp 0) and the paid
     fee; the outcome stays "failed" (the client renders it neutrally).  */
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["outcome"].asString (), "failed");
  EXPECT_EQ (h["mode"].asString (), "ruling");
  EXPECT_EQ (h["settledp"].asUInt (), 0u);
  EXPECT_TRUE (h["feepaid"].asBool ());
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
  EXPECT_FALSE (jobs.GetById (id)->GetProto ().deal ().disputed ());
}

TEST_F (DealTests, ConfirmBarsOwnDisputeWorker)
{
  /* H1, the worker's mirror image of the guard.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_FALSE (Dispute ("courier", id));
  EXPECT_TRUE (JobExists (id));
  EXPECT_FALSE (jobs.GetById (id)->GetProto ().deal ().disputed ());
}

TEST_F (DealTests, ConfirmerCounterpartyMayStillDispute)
{
  /* H1: the poster's confirm does NOT waive the worker's dispute right -- the
     counterparty may still contest a shoddy job.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Dispute ("courier", id));
  EXPECT_TRUE (jobs.GetById (id)->GetProto ().deal ().disputed ());
}

TEST_F (DealTests, WorkerConfirmPosterMayStillDispute)
{
  /* H1: the worker's confirm does NOT waive the poster's dispute right.  */
  const auto id = PostAcceptDeal ();
  EXPECT_TRUE (Confirm ("courier", id));
  EXPECT_TRUE (Dispute ("poster", id));
  EXPECT_TRUE (jobs.GetById (id)->GetProto ().deal ().disputed ());
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "single-confirm");
  EXPECT_EQ (h["settledp"].asUInt (), 100u);
  EXPECT_FALSE (h.isMember ("feepaid"));   // no arbiter bound
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "ghost-split");
  EXPECT_EQ (h["settledp"].asUInt (), 50u);
  EXPECT_FALSE (h["feepaid"].asBool ());   // arbiter bound but unpaid
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "single-confirm");
  EXPECT_EQ (h["settledp"].asUInt (), 100u);
  EXPECT_TRUE (h["feepaid"].asBool ());
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "refund");
  EXPECT_FALSE (h.isMember ("settledp"));
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "ghost-split");
  EXPECT_EQ (h["settledp"].asUInt (), 50u);
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "single-confirm");
  EXPECT_EQ (h["settledp"].asUInt (), 100u);
  EXPECT_FALSE (h.isMember ("feepaid"));
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "ruling");
  EXPECT_EQ (h["settledp"].asUInt (), 50u);
  EXPECT_TRUE (h["feepaid"].asBool ());
  EXPECT_EQ (h["disputetime"].asInt64 (), BASE_TS + DAY - 1);
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "ghost-split");
  EXPECT_EQ (h["settledp"].asUInt (), 50u);
  EXPECT_FALSE (h.isMember ("feepaid"));
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
  EXPECT_EQ (HistoryJson (id)["mode"].asString (), "ruling");
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
  EXPECT_EQ (HistoryJson (id)["mode"].asString (), "single-confirm");
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
  EXPECT_EQ (HistoryJson (id)["mode"].asString (), "single-confirm");
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
  EXPECT_EQ (HistoryJson (id)["mode"].asString (), "refund");
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
  EXPECT_EQ (HistoryJson (id)["disputetime"].asInt64 (), BASE_TS + 100);
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
  /* getjobsparams returns the POST-CLAMP effective value consensus uses: each
     ceiling caps an over-ceiling override, a negative override floors to 0, and
     the load-bearing prune-batch floor is 1.  */
  params.Set ("max-live-jobs", CAP_MAX_LIVE_JOBS + 5);
  params.Set ("max-jobs-per-poster", -7);
  params.Set ("max-bounty-pools-per-target", CAP_MAX_BOUNTY_POOLS_PER_TARGET + 1);
  params.Set ("jobs-history-prune-batch", 0);
  GameStateJson gsj(db, ctx);
  const Json::Value p = gsj.JobsParams ();
  EXPECT_EQ (p["max-live-jobs"].asInt64 (), CAP_MAX_LIVE_JOBS);
  EXPECT_EQ (p["max-jobs-per-poster"].asInt64 (), 0);
  EXPECT_EQ (p["max-bounty-pools-per-target"].asInt64 (),
             CAP_MAX_BOUNTY_POOLS_PER_TARGET);
  EXPECT_EQ (p["jobs-history-prune-batch"].asInt64 (), 1);
  /* A self-bounding param reports the raw overlay (no clamp).  */
  params.Set ("deal-tax-bps", 4321);
  EXPECT_EQ (gsj.JobsParams ()["deal-tax-bps"].asInt64 (), 4321);
}

TEST_F (DealTests, RetentionOverrideAndZeroPruneBatchSweepNoHalt)
{
  /* The sweep reads the retention + prune-batch runtime overlay: a launch-window
     retention override is honoured, and an admin-stored prune-batch of 0 (a
     legal ParamsTable value) is clamped to 1 at the ExpireJobs read so the
     superblock crosses with NO CHECK-halt and still prunes.  */
  const auto id = PostAcceptDeal ();             // settles -> history at BASE_TS
  EXPECT_TRUE (Confirm ("poster", id));
  EXPECT_TRUE (Confirm ("courier", id));
  ASSERT_FALSE (HistoryJson (id).isNull ());
  params.Set ("jobs-history-retention", 10);     // override the 180-day default
  params.Set ("jobs-history-prune-batch", 0);    // the load-bearing zero
  ctx.SetTimestamp (BASE_TS + 100);              // past the override cutoff
  ExpireJobs (db, ctx);                          // must not halt
  EXPECT_TRUE (HistoryJson (id).isNull ());       // pruned with batch clamped to 1
}

TEST_F (DealTests, ProBonoArbiterFeePaidHonoursScheduleAtZeroFee)
{
  /* L4: fee_paid records that the agreed fee SCHEDULE was honoured, not that
     coins moved.  A pro-bono arbiter (bound, fee_bps 0) that carries the deal
     to a both-confirm settle honoured its schedule, so history stamps feepaid
     true even though the arbiter receives nothing -- distinguishing an honest
     zero-fee settle from a forfeited fee (which stamps false).  */
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
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["mode"].asString (), "both-confirm");
  EXPECT_EQ (h["settledp"].asUInt (), 100u);
  EXPECT_TRUE (h["feepaid"].asBool ());        // schedule honoured, not forfeited
}

TEST_F (DealTests, OpenDealExpiresVoidWithoutSettlementKeys)
{
  /* L6a: an OPEN deal that is never accepted expires through the void hook,
     which refunds the poster's reward and touches no deal proto -- the deal
     settlement path (RefundBothDeal / SettleDeal) never runs.  So the history
     row records outcome void with NONE of the settlement keys, not even a
     feepaid, despite an arbiter being named in the posted terms.  */
  const auto id = PostDeal ();   // arbiter named, but never accepted
  Expire ();
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 50);   // reward refunded, fee burned
  const Json::Value h = HistoryJson (id);
  EXPECT_EQ (h["outcome"].asString (), "void");
  EXPECT_FALSE (h.isMember ("mode"));
  EXPECT_FALSE (h.isMember ("settledp"));
  EXPECT_FALSE (h.isMember ("feepaid"));
}

TEST_F (AdTests, PostRejectsNullStart)
{
  /* An explicit null "start" is rejected like every other malformed term
     (strict grammar): a client serialising "unset" as null must hear the
     rejection, not buy an immediate window it did not intend.  */
  EXPECT_FALSE (Process ("courier",
      R"({"t":"ad","d":86400,"r":500,"co":0,"b":1,"slot":2,)"
      R"("hash":"abc","start":null})"));
}

TEST_F (AdTests, AssignRejected)
{
  /* An ad's designation is type-managed (pinned to the building owner,
     whose accept is the approval): re-designating could only strand the
     listing, so the assign is rejected and the designation keeps working.  */
  const auto id = PostAd ();
  EXPECT_FALSE (Process ("courier",
      R"({"s":)" + std::to_string (id) + R"(,"w":"courier2"})"));
  EXPECT_EQ (jobs.GetById (id)->GetProto ().designated_worker (), "poster");
  EXPECT_TRUE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));
}

/* -- concentrated-cohort liveness benches (design escrow-v1.1 §8) ---------- *
   The v15/v16 stress covered only the distributed expiry sweep; these two
   paths (one entity's whole linked cohort dying at once, one target's whole
   pool stack paid on one kill) were un-benched.  Timed at the NEW ceilings and
   reported so the per-block budget is checked before cutover.
   ------------------------------------------------------------------------- */

template <typename Fn>
  double
  TimedMillis (Fn&& fn)
{
  const auto t0 = std::chrono::steady_clock::now ();
  fn ();
  const auto t1 = std::chrono::steady_clock::now ();
  return std::chrono::duration<double, std::milli> (t1 - t0).count ();
}

TEST_F (AdTests, LinkedEntityCapRejectsOverflowPost)
{
  /* The per-entity admission cap's REJECT branch: the cohort bench below only
     proves that N posts are ADMITTED, so nothing covered the refusal.  With
     the cap at 2, the third ad linked to the same building is rejected and the
     board stays at 2.  */
  static constexpr const char* AD =
      R"({"t":"ad","d":86400,"r":10,"co":0,"b":1,"slot":0,"hash":"abc"})";
  params.Set ("max-jobs-per-linked-entity", 2);
  ASSERT_TRUE (Process ("courier", AD));
  ASSERT_TRUE (Process ("courier", AD));
  EXPECT_FALSE (Process ("courier", AD));
  EXPECT_EQ (jobs.CountForLinkedId (1), 2);
}

TEST_F (AdTests, LinkedEntityDestructionCohortBench)
{
  /* One building carrying the ceiling of linked ad jobs, all settled in the
     single block it dies (OnJobEntityDestroyed -> SettleLinkedJobs).  */
  const unsigned N = CAP_MAX_JOBS_PER_LINKED_ENTITY;   // 1000
  params.Set ("max-jobs-per-poster", CAP_MAX_JOBS_PER_POSTER);
  params.Set ("max-jobs-per-linked-entity", CAP_MAX_JOBS_PER_LINKED_ENTITY);
  for (unsigned i = 0; i < N; ++i)
    ASSERT_TRUE (Process ("courier",
        R"({"t":"ad","d":86400,"r":10,"co":0,"b":1,"slot":0,"hash":"abc"})"));
  EXPECT_EQ (jobs.CountForLinkedId (1), N);

  const double ms = TimedMillis ([this] { OnJobEntityDestroyed (db, ctx, 1); });
  LOG (INFO) << "[bench] destroyed " << N << " linked ad jobs in " << ms << " ms";

  EXPECT_EQ (jobs.CountAll (), 0);                     // all settled off the board
  EXPECT_EQ (Balance ("courier"), 1000000 - N);   // only the burned fees gone
}

TEST_F (WantedTests, BountyPoolKillCohortBench)
{
  /* One target carrying the ceiling of stacked bounty pools, all paid in the
     single block a qualifying kill lands (UpdateForKill -> PayKillShares).  */
  const unsigned N = CAP_MAX_BOUNTY_POOLS_PER_TARGET;   // 100
  params.Set ("max-jobs-per-poster", CAP_MAX_JOBS_PER_POSTER);
  params.Set ("max-bounty-pools-per-target", CAP_MAX_BOUNTY_POOLS_PER_TARGET);
  for (unsigned i = 0; i < N; ++i)
    ASSERT_TRUE (Process ("poster",
        R"({"t":"wanted","r":100,"co":0,"name":"green","n":1})"));
  EXPECT_EQ (jobs.CountForLinkedName ("green"), N);

  const auto victim = MakeCharacterAt ("green", HexCoord (5, 5));
  const auto hunter = MakeCharacterAt ("courier", HexCoord (6, 5));
  DamageLists dl(db, ctx.Height ());
  dl.AddEntry (victim, hunter);
  proto::TargetId target;
  target.set_type (proto::TargetId::TYPE_CHARACTER);
  target.set_id (victim);

  const double ms = TimedMillis ([&] {
      JobsBountyTracker tracker(db, ctx, dl);
      tracker.UpdateForKill (target);
    });
  LOG (INFO) << "[bench] paid " << N << " bounty pools on one kill in "
             << ms << " ms";

  EXPECT_EQ (jobs.CountAll (), 0);                       // all pools drained
  EXPECT_EQ (Balance ("courier"), 1000000 + N * 100);
}

} // anonymous namespace
} // namespace pxd
