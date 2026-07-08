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
#include "testutils.hpp"

#include "database/dbtest.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <map>
#include <string>

namespace pxd
{
namespace
{

/** A comfortable base consensus timestamp for the tests.  */
constexpr int64_t BASE_TS = 1000000;
/** A comfortable job duration (within [min,max]_job_duration).  */
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
  BuildingInventoriesTable buildingInv;
  CharacterTable characters;
  OngoingsTable ongoings;
  JobsTable jobs;

  ContextForTesting ctx;

  JobsTests ()
    : accounts(db), buildings(db), buildingInv(db), characters(db),
      ongoings(db), jobs(db)
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
  }

  JobContext
  Ctx ()
  {
    return {ctx, accounts, buildings, buildingInv, characters, ongoings, jobs};
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

  Quantity
  ItemBalance (const Database::IdT building, const std::string& name,
               const std::string& item)
  {
    return buildingInv.Get (building, name)
        ->GetInventory ().GetFungibleCount (item);
  }

  /** Stages fungible goods into an account's own inventory at a building.  */
  void
  StageGoods (const Database::IdT building, const std::string& name,
              const std::map<std::string, Quantity>& items)
  {
    auto inv = buildingInv.Get (building, name);
    for (const auto& entry : items)
      inv->GetInventory ().AddFungibleCount (entry.first, entry.second);
  }

  /**
   * Creates a character owned by the account, docked at the building, with the
   * given cargo, and returns its ID.
   */
  Database::IdT
  MakeCharacter (const std::string& owner, const Database::IdT building,
                 const std::map<std::string, Quantity>& cargo)
  {
    auto c = characters.CreateNew (owner, accounts.GetByName (owner)
                                              ->GetFaction ());
    const auto id = c->GetId ();
    c->SetBuildingId (building);
    for (const auto& entry : cargo)
      c->GetInventory ().AddFungibleCount (entry.first, entry.second);
    return id;
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

  bool
  JobExists (const Database::IdT id)
  {
    return jobs.GetById (id) != nullptr;
  }

  /** Posts a standard foo:5 transport job (reward 2000, collateral 8000).  */
  Database::IdT
  Post (const std::string& to = "1")
  {
    CHECK (Process ("poster",
        R"({"t":"transport","d":86400,"r":2000,"co":8000,"to":)" + to
        + R"(,"items":{"foo":5}})"));
    return OnlyJobId ();
  }

  /** Posts and has courier accept; returns the job ID.  */
  Database::IdT
  PostAndAccept ()
  {
    const auto id = Post ();
    CHECK (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
    return id;
  }

};

/* ************************************************************************** */
/* Parsing (strict discriminator).                                            */

TEST_F (JobsTests, ParsingValidShapes)
{
  EXPECT_TRUE (ParseOk (
      R"({"t":"transport","d":86400,"r":2000,"co":8000,"to":1,"items":{"foo":5}})"));
  EXPECT_TRUE (ParseOk (R"({"s":7,"w":"courier"})"));
  EXPECT_TRUE (ParseOk (R"({"a":7})"));
  EXPECT_TRUE (ParseOk (R"({"c":7})"));
  EXPECT_TRUE (ParseOk (R"({"f":7})"));
  EXPECT_TRUE (ParseOk (R"({"f":7,"ch":42})"));
}

TEST_F (JobsTests, ParsingRejects)
{
  /* Zero discriminator keys.  */
  EXPECT_FALSE (ParseOk (R"({"d":86400,"r":2000})"));
  /* Two discriminator keys.  */
  EXPECT_FALSE (ParseOk (R"({"a":7,"c":8})"));
  EXPECT_FALSE (ParseOk (R"({"t":"transport","a":7})"));
  /* Unknown job type.  */
  EXPECT_FALSE (ParseOk (
      R"({"t":"teleport","d":86400,"r":1,"co":0,"to":1,"items":{"foo":1}})"));
  /* Non-integer id / non-object move.  */
  EXPECT_FALSE (ParseOk (R"({"a":"seven"})"));
  EXPECT_FALSE (ParseOk (R"([1,2,3])"));
  /* Extra members on fixed-shape ops.  */
  EXPECT_FALSE (ParseOk (R"({"a":7,"extra":1})"));
  EXPECT_FALSE (ParseOk (R"({"f":7,"bogus":1})"));
  /* Assign without the worker field.  */
  EXPECT_FALSE (ParseOk (R"({"s":7})"));
  /* Post with a missing deadline.  */
  EXPECT_FALSE (ParseOk (
      R"({"t":"transport","r":2000,"co":8000,"to":1,"items":{"foo":5}})"));
}

/* ************************************************************************** */
/* Post.                                                                      */

TEST_F (JobsTests, PostHappy)
{
  const auto id = Post ();
  auto j = jobs.GetById (id);
  ASSERT_NE (j, nullptr);
  EXPECT_EQ (j->GetStatus (), Job::Status::OPEN);
  EXPECT_EQ (j->GetFaction (), Faction::RED);
  EXPECT_EQ (j->GetLinkedId (), 1);
  ASSERT_TRUE (j->HasDeadline ());
  EXPECT_EQ (j->GetDeadline (), BASE_TS + DAY);
  EXPECT_EQ (j->GetProto ().transport ().manifest ().fungible ().at ("foo"), 5);
  /* reward 2000 locked + fee max(1, 2000*100/10000 = 20) = 20 burned.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 2000 - 20);
}

TEST_F (JobsTests, PostRejects)
{
  /* Cannot afford reward + fee.  */
  MakeAccount ("broke", Faction::RED, 100);
  EXPECT_FALSE (Process ("broke",
      R"({"t":"transport","d":86400,"r":2000,"co":0,"to":1,"items":{"foo":5}})"));
  /* Duration below the minimum and above the maximum.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":1,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":999999999,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  /* Empty manifest / unknown item.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"r":10,"co":0,"to":1,"items":{}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"r":10,"co":0,"to":1,"items":{"nope":5}})"));
  /* Nonexistent and enemy-faction destinations.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"r":10,"co":0,"to":999,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"r":10,"co":0,"to":3,"items":{"foo":5}})"));
  /* An uninitialised account cannot post.  */
  accounts.CreateNew ("nofaction");
  EXPECT_FALSE (Process ("nofaction",
      R"({"t":"transport","d":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  /* Neutral (ancient) destination is allowed.  */
  EXPECT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"r":10,"co":0,"to":2,"items":{"foo":5}})"));
}

/* ************************************************************************** */
/* Accept.                                                                    */

TEST_F (JobsTests, AcceptHappy)
{
  const auto id = Post ();
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  auto j = jobs.GetById (id);
  EXPECT_EQ (j->GetStatus (), Job::Status::ACCEPTED);
  EXPECT_EQ (j->GetWorker (), "courier");
  EXPECT_EQ (Balance ("courier"), 1000000 - 8000);
}

TEST_F (JobsTests, AcceptRejects)
{
  const auto id = Post ();
  const std::string a = R"({"a":)" + std::to_string (id) + "}";
  /* Poster cannot accept own job.  */
  EXPECT_FALSE (Process ("poster", a));
  /* Wrong faction cannot accept.  */
  EXPECT_FALSE (Process ("green", a));
  /* Cannot afford the collateral.  */
  MakeAccount ("poorcourier", Faction::RED, 100);
  EXPECT_FALSE (Process ("poorcourier", a));
  /* A non-OPEN job cannot be accepted again.  */
  ASSERT_TRUE (Process ("courier", a));
  EXPECT_FALSE (Process ("courier2", a));
}

TEST_F (JobsTests, AcceptZeroCollateralIsLegal)
{
  ASSERT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"r":2000,"co":0,"to":1,"items":{"foo":5}})"));
  const auto id = OnlyJobId ();
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_EQ (Balance ("courier"), 1000000);
}

TEST_F (JobsTests, AcceptRunwayGuard)
{
  /* Post a minimum-duration job, then advance time so almost no runway is
     left: accepting must be rejected.  */
  ASSERT_TRUE (Process ("poster",
      R"({"t":"transport","d":3600,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  const auto id = OnlyJobId ();
  ctx.SetTimestamp (BASE_TS + 3600 - 100);
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
}

/* ************************************************************************** */
/* Assign.                                                                    */

TEST_F (JobsTests, AssignAndDesignationGate)
{
  const auto id = Post ();
  const std::string s = R"({"s":)" + std::to_string (id);
  /* Non-poster cannot assign; cannot self-designate; unknown/wrong-faction
     workers are rejected.  */
  EXPECT_FALSE (Process ("courier", s + R"(,"w":"courier"})"));
  EXPECT_FALSE (Process ("poster", s + R"(,"w":"poster"})"));
  EXPECT_FALSE (Process ("poster", s + R"(,"w":"ghost"})"));
  EXPECT_FALSE (Process ("poster", s + R"(,"w":"green"})"));
  /* Poster designates courier.  */
  ASSERT_TRUE (Process ("poster", s + R"(,"w":"courier"})"));
  EXPECT_EQ (jobs.GetById (id)->GetProto ().designated_worker (), "courier");

  /* Now only the designated worker may accept (transport is not
     approval-required, but a set designation still restricts it).  */
  EXPECT_FALSE (Process ("courier2", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));

  /* Assigning after acceptance is rejected.  */
  EXPECT_FALSE (Process ("poster", s + R"(,"w":"courier2"})"));
}

/* ************************************************************************** */
/* Cancel.                                                                    */

TEST_F (JobsTests, CancelRefundsPoster)
{
  const auto id = Post ();
  ASSERT_EQ (Balance ("poster"), 1000000 - 2000 - 20);
  ASSERT_TRUE (Process ("poster", R"({"c":)" + std::to_string (id) + "}"));
  EXPECT_FALSE (JobExists (id));
  /* Reward refunded; the fee stays burned.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
}

TEST_F (JobsTests, CancelRejects)
{
  const auto id = Post ();
  /* Non-poster cannot cancel.  */
  EXPECT_FALSE (Process ("courier", R"({"c":)" + std::to_string (id) + "}"));
  /* An accepted job cannot be cancelled.  */
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_FALSE (Process ("poster", R"({"c":)" + std::to_string (id) + "}"));
}

/* ************************************************************************** */
/* Fulfil.                                                                    */

TEST_F (JobsTests, FulfilFromCharacterCargo)
{
  const auto id = PostAndAccept ();
  const auto ch = MakeCharacter ("courier", 1, {{"foo", 5}});

  ASSERT_TRUE (Process ("courier",
      R"({"f":)" + std::to_string (id) + R"(,"ch":)" + std::to_string (ch)
      + "}"));
  EXPECT_FALSE (JobExists (id));
  /* Goods delivered to the poster; worker paid reward + returned collateral.  */
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 5);
  EXPECT_EQ (Balance ("courier"), 1000000 - 8000 + 2000 + 8000);
}

TEST_F (JobsTests, FulfilFromOwnInventoryMultiTrip)
{
  const auto id = PostAndAccept ();
  /* The worker staged the goods into their own inventory at B over trips.  */
  StageGoods (1, "courier", {{"foo", 5}});

  ASSERT_TRUE (Process ("courier", R"({"f":)" + std::to_string (id) + "}"));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 5);
  EXPECT_EQ (ItemBalance (1, "courier", "foo"), 0);
}

TEST_F (JobsTests, FulfilRejects)
{
  const auto id = PostAndAccept ();
  const std::string base = R"({"f":)" + std::to_string (id);

  /* Character not docked at the destination.  */
  const auto chElsewhere = MakeCharacter ("courier", 2, {{"foo", 5}});
  EXPECT_FALSE (Process ("courier",
      base + R"(,"ch":)" + std::to_string (chElsewhere) + "}"));

  /* Character docked but short on the manifest.  */
  const auto chShort = MakeCharacter ("courier", 1, {{"foo", 4}});
  EXPECT_FALSE (Process ("courier",
      base + R"(,"ch":)" + std::to_string (chShort) + "}"));

  /* Own-inventory variant with nothing staged.  */
  EXPECT_FALSE (Process ("courier", base + "}"));

  /* A different account (not the worker) cannot fulfil.  */
  StageGoods (1, "courier2", {{"foo", 5}});
  EXPECT_FALSE (Process ("courier2", base + "}"));

  /* Nothing was consumed by the failed attempts.  */
  EXPECT_TRUE (JobExists (id));
}

TEST_F (JobsTests, FulfilRejectsNonAccepted)
{
  const auto id = Post ();
  const auto ch = MakeCharacter ("courier", 1, {{"foo", 5}});
  /* OPEN (not ACCEPTED) job cannot be fulfilled.  */
  EXPECT_FALSE (Process ("courier",
      R"({"f":)" + std::to_string (id) + R"(,"ch":)" + std::to_string (ch)
      + "}"));
}

/* ************************************************************************** */
/* Expiry.                                                                    */

TEST_F (JobsTests, ExpireOpenRefundsPoster)
{
  const auto id = Post ();
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
}

TEST_F (JobsTests, ExpireAcceptedForfeitsToPoster)
{
  const auto id = PostAndAccept ();
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  /* Reward refunds AND the worker's collateral forfeits, both to the poster.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 20 + 8000);
  EXPECT_EQ (Balance ("courier"), 1000000 - 8000);
}

TEST_F (JobsTests, ExpireIdleBlockTouchesNothing)
{
  const auto id = Post ();
  /* Well before the deadline: nothing is due.  */
  ExpireJobs (db, ctx);
  EXPECT_TRUE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 2000 - 20);
}

/* ************************************************************************** */
/* Linked-entity (building) destruction hook.                                 */

TEST_F (JobsTests, DestroyDestinationVoidsOpen)
{
  const auto id = Post ();
  OnBuildingDestroyed (db, ctx, 1);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
}

TEST_F (JobsTests, DestroyDestinationRefundsBothWhenAccepted)
{
  const auto id = PostAndAccept ();
  OnBuildingDestroyed (db, ctx, 1);
  EXPECT_FALSE (JobExists (id));
  /* Not the worker's fault: reward to poster, collateral back to worker.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
  EXPECT_EQ (Balance ("courier"), 1000000);
}

TEST_F (JobsTests, DestroyUnrelatedBuildingIsNoOp)
{
  const auto id = Post ();
  OnBuildingDestroyed (db, ctx, 2);
  EXPECT_TRUE (JobExists (id));
}

/* ************************************************************************** */
/* Invariants.                                                                */

TEST_F (JobsTests, CoinConservationAcrossLifecycle)
{
  const Amount before = Balance ("poster") + Balance ("courier");
  const auto id = PostAndAccept ();
  const auto ch = MakeCharacter ("courier", 1, {{"foo", 5}});
  ASSERT_TRUE (Process ("courier",
      R"({"f":)" + std::to_string (id) + R"(,"ch":)" + std::to_string (ch)
      + "}"));
  /* Everything reserved is settled; only the burned fee (20) has left the
     two accounts.  */
  EXPECT_EQ (Balance ("poster") + Balance ("courier"), before - 20);
}

TEST_F (JobsTests, SameBlockKillBeatsExpiry)
{
  /* A destination that dies in the same block its deadline passes is a death,
     not a survival: the kill hook (which runs first) refunds the worker's
     collateral rather than the expiry sweep forfeiting it.  */
  const auto id = PostAndAccept ();
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  OnBuildingDestroyed (db, ctx, 1);   // kill processing runs before the sweep
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
  EXPECT_EQ (Balance ("courier"), 1000000);
}

} // anonymous namespace
} // namespace pxd
