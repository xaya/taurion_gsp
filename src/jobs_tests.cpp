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

#include "combat.hpp"
#include "jsonutils.hpp"
#include "testutils.hpp"

#include "database/dbtest.hpp"
#include "database/params.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

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
/** A comfortable window length (valid as both a listing and a work window).  */
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
  BuildingInventoriesTable buildingInv;
  CharacterTable characters;
  OngoingsTable ongoings;
  GroundLootTable groundLoot;
  JobsTable jobs;

  ContextForTesting ctx;

  JobsTests ()
    : accounts(db), buildings(db), buildingInv(db), characters(db),
      ongoings(db), groundLoot(db), jobs(db)
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
       them exactly as an admin would.  MinimumRewardDefaults removes these
       overrides to exercise the real defaults.  */
    ParamsTable par(db);
    par.Set ("min-job-reward", 1);
    par.Set ("min-bounty-reward", 1);
  }

  JobContext
  Ctx ()
  {
    return {db, ctx, accounts, buildings, buildingInv, characters, ongoings,
            groundLoot, jobs};
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

  /** Creates a foundation of the given faction/owner and returns its ID.  */
  Database::IdT
  MakeFoundation (const std::string& owner, const Faction f)
  {
    auto b = buildings.CreateNew ("checkmark", owner, f);
    b->MutableProto ().set_foundation (true);
    return b->GetId ();
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

  /**
   * Destroys the building through the real combat kill path (ProcessKills ->
   * ProcessBuilding), so that settlement of jobs linked to characters docked
   * inside it is exercised end to end -- not by calling OnJobEntityDestroyed
   * directly.
   */
  void
  DestroyBuildingViaCombat (const Database::IdT id)
  {
    proto::TargetId target;
    target.set_type (proto::TargetId::TYPE_BUILDING);
    target.set_id (id);

    DynObstacles dyn(db, ctx);
    DamageLists dmg(db, ctx.Height ());
    TestRandom rnd;
    ProcessKills (db, dyn, dmg, groundLoot, {target}, rnd, ctx);
  }

  /** Returns (completed, failed, failed-as-poster, value) for an account.  */
  std::tuple<unsigned, unsigned, unsigned, Amount>
  JobStats (const std::string& name)
  {
    const auto& pb = accounts.GetByName (name)->GetProto ();
    return {pb.jobs_completed (), pb.jobs_failed (),
            pb.jobs_failed_as_poster (), pb.jobs_value_completed ()};
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

  /** Posts a standard foo:5 transport job (reward 2000, collateral 8000).  */
  Database::IdT
  Post (const std::string& to = "1")
  {
    CHECK (Process ("poster",
        R"({"t":"transport","d":86400,"wd":86400,"r":2000,"co":8000,"to":)" + to
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

  /**
   * Posts an approval-required job, assigns it to the worker and has them
   * accept.  Returns the job ID.
   */
  Database::IdT
  PostAssignAccept (const std::string& post, const std::string& worker)
  {
    CHECK (Process ("poster", post));
    const auto id = LatestJobId ();
    CHECK (Process ("poster",
        R"({"s":)" + std::to_string (id) + R"(,"w":")" + worker + R"("})"));
    CHECK (Process (worker, R"({"a":)" + std::to_string (id) + "}"));
    return id;
  }

};

/* ************************************************************************** */
/* Parsing (strict discriminator).                                            */

TEST_F (JobsTests, ParsingValidShapes)
{
  /* One full-key shape per type, so the strict POST key filter provably
     admits every legal term key (values are IsValid's business).  */
  EXPECT_TRUE (ParseOk (
      R"({"t":"transport","d":86400,"wd":86400,"r":2000,"co":8000,"to":1,"items":{"foo":5}})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"haul","d":86400,"wd":86400,"r":2000,"co":8000,"from":1,"to":2,"items":{"foo":5}})"));
  EXPECT_TRUE (ParseOk (R"({"t":"wanted","r":9000,"co":0,"name":"x","n":3})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"protect","d":86400,"wd":86400,"r":2000,"co":8000,"b":1})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"destroy","d":86400,"wd":86400,"r":2000,"co":8000,"b":1})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"escort","d":86400,"wd":86400,"r":2000,"co":8000,"ch":42,"to":1})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"bodyguard","d":86400,"wd":86400,"r":2000,"co":8000,"ch":42})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"patrol","d":86400,"wd":86400,"r":2000,"co":8000,"x":0,"y":0,"rad":5,"k":3,"sp":600})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"rental","d":86400,"wd":86400,"r":2000,"co":0,"b":1,"i":"foo","n":5,"rent":100,"w":"courier"})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"ad","d":86400,"r":2000,"co":0,"b":1,"slot":0,"hash":"abc","start":3600})"));
  EXPECT_TRUE (ParseOk (
      R"({"t":"toll","d":86400,"wd":86400,"r":2000,"co":0,"ch":42,"w":"courier"})"));
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
  /* POST is exactly as strict: an unknown key rejects for every predicate
     family, including another type's legal key and a typo'd one.  */
  EXPECT_FALSE (ParseOk (
      R"({"t":"transport","d":86400,"wd":86400,"r":2000,"co":8000,"to":1,"items":{"foo":5},"extra":1})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"transport","d":86400,"wd":86400,"r":2000,"co":8000,"to":1,"items":{"foo":5},"from":1})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"haul","d":86400,"wd":86400,"r":2000,"co":8000,"from":1,"to":2,"items":{"foo":5},"item":{}})"));
  EXPECT_FALSE (ParseOk (R"({"t":"wanted","r":9000,"co":0,"name":"x","n":3,"nn":3})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"protect","d":86400,"wd":86400,"r":2000,"co":8000,"b":1,"ch":42})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"destroy","d":86400,"wd":86400,"r":2000,"co":8000,"b":1,"bb":1})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"escort","d":86400,"wd":86400,"r":2000,"co":8000,"ch":42,"to":1,"b":1})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"bodyguard","d":86400,"wd":86400,"r":2000,"co":8000,"ch":42,"cch":42})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"patrol","d":86400,"wd":86400,"r":2000,"co":8000,"x":0,"y":0,"rad":5,"k":3,"sp":600,"z":0})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"rental","d":86400,"wd":86400,"r":2000,"co":0,"b":1,"i":"foo","n":5,"rent":100,"w":"courier","deposit":1})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"ad","d":86400,"r":2000,"co":0,"b":1,"slot":0,"hash":"abc","start":3600,"end":7200})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"toll","d":86400,"wd":86400,"r":2000,"co":0,"ch":42,"w":"courier","fee":1})"));
  /* Assign without the worker field.  */
  EXPECT_FALSE (ParseOk (R"({"s":7})"));
  /* Negative deadline / negative work window.  */
  EXPECT_FALSE (ParseOk (
      R"({"t":"transport","d":-5,"r":2000,"co":8000,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"transport","d":86400,"wd":-5,"r":2000,"co":8000,"to":1,"items":{"foo":5}})"));
  /* Integral JSON reals are not integers (the strict-integer convention
     shared with CoinAmountFromJson).  */
  EXPECT_FALSE (ParseOk (
      R"({"t":"transport","d":86400.0,"wd":86400,"r":2000,"co":8000,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (ParseOk (
      R"({"t":"transport","d":86400,"wd":8.64e4,"r":2000,"co":8000,"to":1,"items":{"foo":5}})"));
}

TEST_F (JobsTests, StandingClassMatchesType)
{
  /* Only the standing types may omit the deadline, and they must; a
     standing job has no deadline to rewrite, so a work window on one is
     dead data and rejected too.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","r":2000,"co":8000,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","d":86400,"r":9000,"co":0,"name":"green","n":3})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","wd":86400,"r":9000,"co":0,"name":"green","n":3})"));
  EXPECT_TRUE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":0,"name":"green","n":3})"));
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

TEST_F (JobsTests, AdmissionCapsGlobalAndFreeze)
{
  ParamsTable par(db);
  const char* POST
      = R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})";

  par.Set ("max-live-jobs", 2);
  EXPECT_TRUE (Process ("poster", POST));
  EXPECT_TRUE (Process ("poster", POST));
  /* The board is full -- for everyone.  */
  EXPECT_FALSE (Process ("poster", POST));
  EXPECT_FALSE (Process ("courier", POST));

  /* 0 freezes posting outright; removing the override resets to the
     (roomy) roconfig default.  */
  par.Set ("max-live-jobs", 0);
  EXPECT_FALSE (Process ("poster", POST));
  par.Remove ("max-live-jobs");
  EXPECT_TRUE (Process ("poster", POST));
}

TEST_F (JobsTests, AdmissionCapPerPoster)
{
  ParamsTable par(db);
  const char* POST
      = R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})";

  par.Set ("max-jobs-per-poster", 1);
  EXPECT_TRUE (Process ("poster", POST));
  EXPECT_FALSE (Process ("poster", POST));
  /* Another account still has headroom.  */
  EXPECT_TRUE (Process ("courier", POST));
}

TEST_F (JobsTests, AdmissionCapPerLinkedEntity)
{
  ParamsTable par(db);

  par.Set ("max-jobs-per-linked-entity", 1);
  EXPECT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  /* A different destination entity is unaffected.  */
  EXPECT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":2,"items":{"foo":5}})"));
  /* Haul counts BOTH its buildings, and building 1 (its source) is at the
     cap.  The goods are stocked first, so the rejection is the cap and not
     the inventory check.  */
  buildingInv.Get (1, "poster")->GetInventory ().AddFungibleCount ("foo", 5);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"haul","d":86400,"wd":86400,"r":10,"co":0,"from":1,"to":2,"items":{"foo":5}})"));
}

TEST_F (JobsTests, AdmissionCapPoolsPerTarget)
{
  ParamsTable par(db);
  par.Set ("max-bounty-pools-per-target", 1);

  EXPECT_TRUE (Process ("poster",
      R"({"t":"wanted","r":100,"co":0,"name":"green","n":2})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":100,"co":0,"name":"green","n":2})"));
  /* A different target still has headroom.  */
  EXPECT_TRUE (Process ("poster",
      R"({"t":"wanted","r":100,"co":0,"name":"courier","n":2})"));
}

TEST_F (JobsTests, MinimumRewardDefaults)
{
  ParamsTable par(db);
  par.Remove ("min-job-reward");
  par.Remove ("min-bounty-reward");

  /* The generic floor (roconfig default 100) applies to every type.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":99,"co":0,"to":1,"items":{"foo":5}})"));
  EXPECT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":100,"co":0,"to":1,"items":{"foo":5}})"));

  /* Wanted pools take the higher bounty floor (default 1000): occupying
     one of a target's capped slots must lock real value.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":999,"co":0,"name":"green","n":2})"));
  EXPECT_TRUE (Process ("poster",
      R"({"t":"wanted","r":1000,"co":0,"name":"green","n":2})"));

  /* Both floors are runtime-tunable like the caps.  */
  par.Set ("min-job-reward", 5000);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":100,"co":0,"to":1,"items":{"foo":5}})"));
}

TEST_F (JobsTests, AcceptRelinkCapForHaulDestinations)
{
  ParamsTable par(db);
  par.Set ("max-jobs-per-linked-entity", 2);

  /* Distinct sources, one shared destination: every POST admits (an OPEN
     haul counts against its source), so the accept-time re-check is the
     only gate keeping the destination at its cap.  */
  CHECK_EQ (buildings.CreateNew ("checkmark", "poster", Faction::RED)
                ->GetId (), 4);
  CHECK_EQ (buildings.CreateNew ("checkmark", "poster", Faction::RED)
                ->GetId (), 5);
  for (const auto src : {1, 4, 5})
    StageGoods (src, "poster", {{"foo", 5}});

  std::vector<Database::IdT> ids;
  for (const auto* src : {"1", "4", "5"})
    {
      CHECK (Process ("poster",
          R"({"t":"haul","d":86400,"wd":86400,"r":10,"co":0,"from":)"
          + std::string (src) + R"(,"to":2,"items":{"foo":5}})"));
      ids.push_back (LatestJobId ());
    }

  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (ids[0]) + "}"));
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (ids[1]) + "}"));
  /* The destination is at its cap now: the third haul cannot relink and
     stays OPEN.  */
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (ids[2]) + "}"));
  EXPECT_EQ (jobs.GetById (ids[2])->GetStatus (), Job::Status::OPEN);

  /* Settling one of the accepted hauls frees a slot and reopens the gate.  */
  StageGoods (2, "courier", {{"foo", 5}});
  EXPECT_TRUE (Process ("courier", R"({"f":)" + std::to_string (ids[0]) + "}"));
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (ids[2]) + "}"));
}

TEST_F (JobsTests, AdmissionCapDefaultsAreLive)
{
  /* No cap overrides anywhere: what rejects the boundary posts here is the
     roconfig defaults themselves (25 / 100 / 200 / 10000).  The board is
     stuffed with directly-created rows so that only the boundary posts run
     the real move path.  */
  MakeAccount ("capped", Faction::RED, 10000);

  for (unsigned i = 0; i < 25; ++i)
    jobs.CreateNew (Job::Type::WANTED, Faction::INVALID, "filler", 1, 0)
        ->SetLinkedName ("green");
  EXPECT_FALSE (Process ("poster",
      R"({"t":"wanted","r":100,"co":0,"name":"green","n":2})"));
  EXPECT_TRUE (Process ("poster",
      R"({"t":"wanted","r":100,"co":0,"name":"courier","n":2})"));

  for (unsigned i = 0; i < 100; ++i)
    jobs.CreateNew (Job::Type::TRANSPORT, Faction::RED, "filler", 1, 0)
        ->SetLinkedId (1);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  EXPECT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":2,"items":{"foo":5}})"));

  for (unsigned i = 0; i < 200; ++i)
    jobs.CreateNew (Job::Type::TRANSPORT, Faction::RED, "capped", 1, 0);
  EXPECT_FALSE (Process ("capped",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":2,"items":{"foo":5}})"));
  EXPECT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":2,"items":{"foo":5}})"));

  while (jobs.CountAll () < 10000)
    jobs.CreateNew (Job::Type::TRANSPORT, Faction::RED, "filler", 1, 0);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":2,"items":{"foo":5}})"));
}

TEST_F (JobsTests, PruneBatchedThroughExpiry)
{
  const auto& params = ctx.RoConfig ()->params ();
  const int64_t retention = params.jobs_history_retention ();
  const int64_t batch = params.jobs_history_prune_batch ();
  ASSERT_GT (retention, 0);
  ASSERT_GT (batch, 0);

  /* Age batch + 1 settled rows past the retention cutoff (history rows are
     keyed by job ID, so each needs its own job, settled the same way the
     production path does it: history write, then row delete).  */
  for (int64_t i = 0; i <= batch; ++i)
    {
      auto j = jobs.CreateNew (Job::Type::TRANSPORT, Faction::RED,
                               "poster", 1, 0);
      const auto id = j->GetId ();
      jobs.WriteHistory (*j, JobOutcome::VOID, 50, BASE_TS + i);
      j.reset ();
      jobs.DeleteById (id);
    }

  /* The history reader is page-capped, so count through the cursor.  */
  const auto countHistory = [this] ()
    {
      int64_t n = 0, lastTime = 0, lastId = 0;
      while (true)
        {
          auto res = jobs.QueryHistory (0, lastTime, lastId);
          bool any = false;
          while (res.Step ())
            {
              const auto e = jobs.GetFromResult (res);
              lastTime = e->GetSettledTime ();
              lastId = e->GetId ();
              ++n;
              any = true;
            }
          if (!any)
            return n;
        }
    };
  ASSERT_EQ (countHistory (), batch + 1);

  /* Run the real production sweep with the whole cohort expired: exactly
     one roconfig batch may disappear per superblock, the remainder drains
     on the next one.  */
  ctx.SetTimestamp (BASE_TS + retention + 1000000);
  ExpireJobs (db, ctx);
  EXPECT_EQ (countHistory (), 1);
  ExpireJobs (db, ctx);
  EXPECT_EQ (countHistory (), 0);
}

TEST_F (JobsTests, PostRejects)
{
  /* Cannot afford reward + fee.  */
  MakeAccount ("broke", Faction::RED, 100);
  EXPECT_FALSE (Process ("broke",
      R"({"t":"transport","d":86400,"wd":86400,"r":2000,"co":0,"to":1,"items":{"foo":5}})"));
  /* Zero reward: settling it would farm the jobs_completed counter for just
     the posting fee.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":0,"co":0,"to":1,"items":{"foo":5}})"));
  /* Listing window below the minimum and above the maximum.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":1,"wd":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":999999999,"wd":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  /* Work window missing, below the minimum and above the maximum.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":1,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":999999999,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  /* Empty manifest / unknown item.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":1,"items":{}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":1,"items":{"nope":5}})"));
  /* Nonexistent and enemy-faction destinations.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":999,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":3,"items":{"foo":5}})"));
  /* An uninitialised account cannot post.  */
  accounts.CreateNew ("nofaction");
  EXPECT_FALSE (Process ("nofaction",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  /* Neutral (ancient) destination is allowed.  */
  EXPECT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":10,"co":0,"to":2,"items":{"foo":5}})"));
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
      R"({"t":"transport","d":86400,"wd":86400,"r":2000,"co":0,"to":1,"items":{"foo":5}})"));
  const auto id = OnlyJobId ();
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_EQ (Balance ("courier"), 1000000);
}

TEST_F (JobsTests, AcceptRejectsDueListing)
{
  /* A listing at or past its deadline belongs to the expiry sweep: accepting
     it would resurrect it (the work-window rewrite pushes the deadline past
     the sweep).  The boundary is exclusive, mirroring fulfil.  */
  const auto id = Post ();
  const std::string a = R"({"a":)" + std::to_string (id) + "}";
  ctx.SetTimestamp (BASE_TS + DAY);
  EXPECT_FALSE (Process ("courier", a));
  ctx.SetTimestamp (BASE_TS + DAY + 50);
  EXPECT_FALSE (Process ("courier", a));
  EXPECT_EQ (jobs.GetById (id)->GetStatus (), Job::Status::OPEN);
  /* The sweep then voids it as designed: escrow refunds, fee stays burned.  */
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
  EXPECT_EQ (Balance ("courier"), 1000000);
}

TEST_F (JobsTests, AcceptStartsWorkClock)
{
  /* Accepting rewrites the deadline to accept time + the work window: even
     one second before the listing expires, the worker still gets the full
     window (which is also why the old runway guard and its
     accept-at-the-minimum dead end are gone).  */
  ASSERT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":7200,"r":10,"co":0,"to":1,"items":{"foo":5}})"));
  const auto id = OnlyJobId ();
  EXPECT_EQ (jobs.GetById (id)->GetDeadline (), BASE_TS + DAY);
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_EQ (jobs.GetById (id)->GetDeadline (), BASE_TS + DAY - 1 + 7200);
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

TEST_F (JobsTests, AssignRejectsStandingJobs)
{
  /* A standing job (wanted board) is never accepted by a single worker, so
     designating one is meaningless dead data -- rejected.  A notice-cancel
     gives the board a deadline, but it remains a standing job and the
     assignment stays rejected.  */
  ASSERT_TRUE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":0,"name":"green","n":3})"));
  const auto id = OnlyJobId ();
  const std::string s
      = R"({"s":)" + std::to_string (id) + R"(,"w":"courier"})";
  EXPECT_FALSE (Process ("poster", s));
  ASSERT_TRUE (Process ("poster", R"({"c":)" + std::to_string (id) + "}"));
  ASSERT_TRUE (jobs.GetById (id)->HasDeadline ());
  EXPECT_FALSE (Process ("poster", s));
  EXPECT_EQ (jobs.GetById (id)->GetProto ().designated_worker (), "");
}

TEST_F (JobsTests, AssignRejectsDueListing)
{
  /* A due listing is the sweep's to void (see JobIsDue): a designation
     landing in the move-before-sweep gap would be dead data on a job that
     never runs.  The boundary is exclusive, mirroring accept/fulfil.  */
  const auto id = Post ();
  const std::string s = R"({"s":)" + std::to_string (id);
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Process ("poster", s + R"(,"w":"courier"})"));
  ctx.SetTimestamp (BASE_TS + DAY);
  EXPECT_FALSE (Process ("poster", s + R"(,"w":"courier2"})"));
  ctx.SetTimestamp (BASE_TS + DAY + 50);
  EXPECT_FALSE (Process ("poster", s + R"(,"w":"courier2"})"));
  EXPECT_EQ (jobs.GetById (id)->GetProto ().designated_worker (), "courier");
  EXPECT_EQ (jobs.GetById (id)->GetStatus (), Job::Status::OPEN);
  /* The sweep then voids it as designed: escrow refunds, fee stays burned.  */
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
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

TEST_F (JobsTests, CancelRejectsDueListing)
{
  /* A due listing is the sweep's to void (see JobIsDue): a cancel in the
     move-before-sweep gap would record CANCELLED history for a job that
     expired.  The refund is identical either way -- only the history label
     is at stake.  The boundary is exclusive, mirroring accept/fulfil.  */
  const auto idCancelled = Post ();
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  ASSERT_TRUE (Process ("poster",
                        R"({"c":)" + std::to_string (idCancelled) + "}"));

  /* The second listing's deadline is relative to the moved timestamp.  */
  const auto idVoid = Post ();
  const std::string c = R"({"c":)" + std::to_string (idVoid) + "}";
  ctx.SetTimestamp (BASE_TS + 2 * DAY - 1);
  EXPECT_FALSE (Process ("poster", c));
  ctx.SetTimestamp (BASE_TS + 2 * DAY + 49);
  EXPECT_FALSE (Process ("poster", c));
  EXPECT_EQ (jobs.GetById (idVoid)->GetStatus (), Job::Status::OPEN);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (idVoid));
  /* Both rewards refunded; the two posting fees stay burned.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 2 * 20);

  /* The pre-deadline cancel settles CANCELLED; the due one VOID.  */
  std::map<Database::IdT, JobOutcome> outcomes;
  auto res = jobs.QueryHistory (0);
  while (res.Step ())
    {
      auto e = jobs.GetFromResult (res);
      outcomes[e->GetId ()] = e->GetOutcome ();
    }
  ASSERT_EQ (outcomes.size (), 2);
  EXPECT_EQ (outcomes.at (idCancelled), JobOutcome::CANCELLED);
  EXPECT_EQ (outcomes.at (idVoid), JobOutcome::VOID);
}

/* ************************************************************************** */
/* Fulfil (delivery).                                                         */

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

TEST_F (JobsTests, FulfilRejectedAtOrAfterDeadline)
{
  const auto id = PostAndAccept ();       // deadline = BASE_TS + DAY
  StageGoods (1, "courier", {{"foo", 5}}); // goods ready: only the deadline can reject

  /* At the deadline the job is already due for the expiry sweep, so it may no
     longer be fulfilled.  */
  ctx.SetTimestamp (BASE_TS + DAY);
  EXPECT_FALSE (Process ("courier", R"({"f":)" + std::to_string (id) + "}"));
  EXPECT_TRUE (JobExists (id));

  /* Past the deadline: still rejected.  */
  ctx.SetTimestamp (BASE_TS + DAY + 100);
  EXPECT_FALSE (Process ("courier", R"({"f":)" + std::to_string (id) + "}"));
  EXPECT_TRUE (JobExists (id));

  /* One second before the deadline: the fulfil goes through.  */
  ctx.SetTimestamp (BASE_TS + DAY - 1);
  EXPECT_TRUE (Process ("courier", R"({"f":)" + std::to_string (id) + "}"));
  EXPECT_FALSE (JobExists (id));
}

TEST_F (JobsTests, FulfilPartialDumpsRecordProgress)
{
  /* Andy's bit-by-bit: each cargo dump delivers what the character carries
     toward the outstanding manifest, and the reward settles only when the
     manifest is empty.  */
  const auto id = PostAndAccept ();
  const std::string f = R"({"f":)" + std::to_string (id) + R"(,"ch":)";

  const auto ch1 = MakeCharacter ("courier", 1, {{"foo", 3}});
  ASSERT_TRUE (Process ("courier", f + std::to_string (ch1) + "}"));
  /* Progress: 3 of 5 delivered to the poster, job persists, nothing paid.  */
  EXPECT_TRUE (JobExists (id));
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 3);
  EXPECT_EQ (Balance ("courier"), 1000000 - 8000);
  EXPECT_EQ (jobs.GetById (id)->GetProto ().transport ().manifest ()
                 .fungible ().at ("foo"), 2);

  /* An empty-handed dump is rejected (no free progress ops).  */
  const auto chEmpty = MakeCharacter ("courier", 1, {});
  EXPECT_FALSE (Process ("courier", f + std::to_string (chEmpty) + "}"));

  /* Second trip completes; excess cargo stays with the character.  */
  const auto ch2 = MakeCharacter ("courier", 1, {{"foo", 10}});
  ASSERT_TRUE (Process ("courier", f + std::to_string (ch2) + "}"));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 5);
  EXPECT_EQ (characters.GetById (ch2)->GetInventory ()
                 .GetFungibleCount ("foo"), 8);
  EXPECT_EQ (Balance ("courier"), 1000000 + 2000);
}

TEST_F (JobsTests, FoundationSupplyBitByBit)
{
  /* Construction-supply: the destination is a foundation, so the goods land
     in its construction inventory, delivered over multiple trips.  */
  const auto fnd = MakeFoundation ("poster", Faction::RED);
  ASSERT_TRUE (Process ("poster",
      R"({"t":"transport","d":86400,"wd":86400,"r":2000,"co":0,"to":)"
      + std::to_string (fnd) + R"(,"items":{"foo":6}})"));
  const auto id = OnlyJobId ();
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));

  /* The own-inventory variant is impossible at a foundation.  */
  StageGoods (fnd, "courier", {{"foo", 6}});
  EXPECT_FALSE (Process ("courier", R"({"f":)" + std::to_string (id) + "}"));

  const std::string f = R"({"f":)" + std::to_string (id) + R"(,"ch":)";
  const auto ch1 = MakeCharacter ("courier", fnd, {{"foo", 4}});
  ASSERT_TRUE (Process ("courier", f + std::to_string (ch1) + "}"));
  EXPECT_TRUE (JobExists (id));

  const auto ch2 = MakeCharacter ("courier", fnd, {{"foo", 2}});
  ASSERT_TRUE (Process ("courier", f + std::to_string (ch2) + "}"));
  EXPECT_FALSE (JobExists (id));

  /* All goods are in the construction inventory; the worker got paid.  */
  Inventory cInv(*buildings.GetById (fnd)->MutableProto ()
                     .mutable_construction_inventory ());
  EXPECT_EQ (cInv.GetFungibleCount ("foo"), 6);
  EXPECT_EQ (Balance ("courier"), 1000000 + 2000);
}

TEST_F (JobsTests, FulfilRejects)
{
  const auto id = PostAndAccept ();
  const std::string base = R"({"f":)" + std::to_string (id);

  /* Character not docked at the destination.  */
  const auto chElsewhere = MakeCharacter ("courier", 2, {{"foo", 5}});
  EXPECT_FALSE (Process ("courier",
      base + R"(,"ch":)" + std::to_string (chElsewhere) + "}"));

  /* Character docked but carrying nothing deliverable.  */
  const auto chIrrelevant = MakeCharacter ("courier", 1, {{"zerospace", 4}});
  EXPECT_FALSE (Process ("courier",
      base + R"(,"ch":)" + std::to_string (chIrrelevant) + "}"));

  /* Own-inventory variant with nothing staged, and with only a PARTIAL
     stage: this variant is all-at-once, so partial coverage must reject
     (bit-by-bit delivery is the cargo variant's job).  */
  EXPECT_FALSE (Process ("courier", base + "}"));
  StageGoods (1, "courier", {{"foo", 4}});
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
  /* The failure is recorded on the worker, and the forfeit-as-poster mark
     on the poster who pocketed the bond.  */
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (0u, 1u, 0u, 0));
  EXPECT_EQ (JobStats ("poster"), std::make_tuple (0u, 0u, 1u, 0));
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
/* Settled-jobs history.                                                      */

TEST_F (JobsTests, HistoryRecordsOutcomes)
{
  /* One job through each generic terminal path; each must leave exactly one
     history row with the true outcome (the chain deletes the live rows).  */

  const auto idDone = PostAndAccept ();
  StageGoods (1, "courier", {{"foo", 5}});
  ASSERT_TRUE (Process ("courier",
                        R"({"f":)" + std::to_string (idDone) + "}"));

  const auto idCancel = Post ();
  ASSERT_TRUE (Process ("poster",
                        R"({"c":)" + std::to_string (idCancel) + "}"));

  /* The Post helper requires a lone live job, so run the two expiry cases
     sequentially (deadlines are relative to the moving timestamp).  */
  const auto idVoid = Post ();
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);

  const auto idFail = PostAndAccept ();
  ctx.SetTimestamp (BASE_TS + 2 * DAY + 2);
  ExpireJobs (db, ctx);

  std::map<Database::IdT, JobOutcome> outcomes;
  std::map<Database::IdT, int64_t> times;
  auto res = jobs.QueryHistory (0);
  while (res.Step ())
    {
      auto e = jobs.GetFromResult (res);
      ASSERT_EQ (outcomes.count (e->GetId ()), 0)
          << "duplicate history row for job " << e->GetId ();
      outcomes[e->GetId ()] = e->GetOutcome ();
      times[e->GetId ()] = e->GetSettledTime ();
    }

  ASSERT_EQ (outcomes.size (), 4);
  EXPECT_EQ (outcomes.at (idDone), JobOutcome::COMPLETED);
  EXPECT_EQ (outcomes.at (idCancel), JobOutcome::CANCELLED);
  EXPECT_EQ (outcomes.at (idVoid), JobOutcome::VOID);
  EXPECT_EQ (outcomes.at (idFail), JobOutcome::FAILED);
  EXPECT_EQ (times.at (idFail), BASE_TS + 2 * DAY + 2);
}

TEST_F (JobsTests, HistoryRecordsRealBlockHeight)
{
  /* Moves settle jobs on EVERY block, but the context's Height() is the
     superblock counter (pre-incremented on ordinary blocks).  The history
     row must record the REAL chain block -- like the DEX trade history --
     not a superblock height that may not have happened yet.  */
  ctx.SetHeight (500);
  ctx.SetBlockHeight (1400);

  const auto id = Post ();
  ASSERT_TRUE (Process ("poster", R"({"c":)" + std::to_string (id) + "}"));

  auto res = jobs.QueryHistory (0);
  ASSERT_TRUE (res.Step ());
  auto e = jobs.GetFromResult (res);
  EXPECT_EQ (e->GetId (), id);
  EXPECT_EQ (e->GetSettledHeight (), 1400);
  EXPECT_FALSE (res.Step ());
}

/* ************************************************************************** */
/* Linked-entity (building) destruction hook.                                 */

TEST_F (JobsTests, DestroyDestinationVoidsOpen)
{
  const auto id = Post ();
  OnJobEntityDestroyed (db, ctx, 1);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
}

TEST_F (JobsTests, DestroyDestinationRefundsBothWhenAccepted)
{
  const auto id = PostAndAccept ();
  OnJobEntityDestroyed (db, ctx, 1);
  EXPECT_FALSE (JobExists (id));
  /* Not the worker's fault: reward to poster, collateral back to worker.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
  EXPECT_EQ (Balance ("courier"), 1000000);
}

TEST_F (JobsTests, DestroyUnrelatedBuildingIsNoOp)
{
  const auto id = Post ();
  OnJobEntityDestroyed (db, ctx, 2);
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
  /* The completion is recorded on the worker.  */
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (1u, 0u, 0u, 2000));
}

TEST_F (JobsTests, SameBlockKillBeatsExpiry)
{
  /* A destination that dies in the same block its deadline passes is a death,
     not a survival: the kill hook (which runs first) refunds the worker's
     collateral rather than the expiry sweep forfeiting it.  */
  const auto id = PostAndAccept ();
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  OnJobEntityDestroyed (db, ctx, 1);   // kill processing runs before the sweep
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
  EXPECT_EQ (Balance ("courier"), 1000000);
}

TEST_F (JobsTests, ValidateJobsPasses)
{
  Post ();
  ASSERT_TRUE (Process ("poster",
      R"({"t":"wanted","r":9000,"co":0,"name":"green","n":3})"));
  ValidateJobs (db);
}

/* ************************************************************************** */
/* Haul.                                                                      */

class HaulTests : public JobsTests
{

protected:

  /** Posts a standard haul of foo:5 from building 1 to building 4.  */
  Database::IdT
  PostHaul ()
  {
    CHECK_EQ (buildings.CreateNew ("checkmark", "poster", Faction::RED)
                  ->GetId (), 4);
    StageGoods (1, "poster", {{"foo", 5}});
    CHECK (Process ("poster",
        R"({"t":"haul","d":86400,"wd":86400,"r":2000,"co":8000,"from":1,"to":4,)"
        R"("items":{"foo":5}})"));
    return OnlyJobId ();
  }

};

TEST_F (HaulTests, PostReservesGoods)
{
  const auto id = PostHaul ();
  /* The goods left the poster's inventory at the source and are held by the
     job; the link watches the source while OPEN.  */
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 0);
  auto j = jobs.GetById (id);
  EXPECT_EQ (j->GetLinkedId (), 1);
  EXPECT_EQ (j->GetProto ().haul ().manifest ().fungible ().at ("foo"), 5);
  EXPECT_EQ (j->GetProto ().haul ().source_building (), 1);
  EXPECT_EQ (j->GetProto ().haul ().dest_building (), 4);
}

TEST_F (HaulTests, PostRejects)
{
  CHECK_EQ (buildings.CreateNew ("checkmark", "poster", Faction::RED)
                ->GetId (), 4);
  /* Goods not present at the source.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"haul","d":86400,"wd":86400,"r":10,"co":0,"from":1,"to":4,"items":{"foo":5}})"));
  StageGoods (1, "poster", {{"foo", 5}});
  /* Source == destination; enemy destination; source that is a foundation.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"haul","d":86400,"wd":86400,"r":10,"co":0,"from":1,"to":1,"items":{"foo":5}})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"haul","d":86400,"wd":86400,"r":10,"co":0,"from":1,"to":3,"items":{"foo":5}})"));
  const auto fnd = MakeFoundation ("poster", Faction::RED);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"haul","d":86400,"wd":86400,"r":10,"co":0,"from":)" + std::to_string (fnd)
      + R"(,"to":4,"items":{"foo":5}})"));
}

TEST_F (HaulTests, CancelReturnsGoods)
{
  const auto id = PostHaul ();
  ASSERT_TRUE (Process ("poster", R"({"c":)" + std::to_string (id) + "}"));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 5);
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
}

TEST_F (HaulTests, OpenExpiryReturnsGoods)
{
  PostHaul ();
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 5);
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
}

TEST_F (HaulTests, AcceptRejectedWhenDestinationDead)
{
  /* While OPEN the linked entity watches the SOURCE, so a destroyed
     destination is only caught by re-validating it at accept time --
     otherwise the accepted job would link a dead building and trap the
     worker's collateral on an undeliverable contract.  */
  const auto id = PostHaul ();
  buildings.DeleteById (4);
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
}

TEST_F (HaulTests, AcceptHandsGoodsAndDeliveryCompletes)
{
  const auto id = PostHaul ();
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));

  /* The goods are now the worker's custody at the source, and the job's
     fate follows the destination.  */
  EXPECT_EQ (ItemBalance (1, "courier", "foo"), 5);
  EXPECT_EQ (jobs.GetById (id)->GetLinkedId (), 4);

  /* Haul them over (the test moves them by hand) and deliver via cargo.  */
  {
    auto inv = buildingInv.Get (1, "courier");
    inv->GetInventory ().AddFungibleCount ("foo", -5);
  }
  const auto ch = MakeCharacter ("courier", 4, {{"foo", 5}});
  ASSERT_TRUE (Process ("courier",
      R"({"f":)" + std::to_string (id) + R"(,"ch":)" + std::to_string (ch)
      + "}"));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (ItemBalance (4, "poster", "foo"), 5);
  EXPECT_EQ (Balance ("courier"), 1000000 + 2000);
}

TEST_F (HaulTests, OpenSourceDestroyedDropsGoodsAsLoot)
{
  const auto id = PostHaul ();
  OnJobEntityDestroyed (db, ctx, 1);
  EXPECT_FALSE (JobExists (id));
  /* Reward refunds; the reserved goods drop at the source's centre.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 20);
  const auto centre = buildings.GetById (1)->GetCentre ();
  EXPECT_EQ (groundLoot.GetByCoord (centre)->GetInventory ()
                 .GetFungibleCount ("foo"), 5);
}

TEST_F (HaulTests, AcceptedDestinationDestroyedCompensatesPoster)
{
  const auto id = PostHaul ();
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  OnJobEntityDestroyed (db, ctx, 4);
  EXPECT_FALSE (JobExists (id));
  /* The worker keeps the goods it already holds, so the collateral (8000)
     forfeits to the poster to compensate, alongside the reward refund; the
     goods stay with the worker and neither side is marked at fault.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 20 + 8000);
  EXPECT_EQ (Balance ("courier"), 1000000 - 8000);
  EXPECT_EQ (ItemBalance (1, "courier", "foo"), 5);
  EXPECT_EQ (accounts.GetByName ("courier")->GetProto ().jobs_failed (), 0);
  EXPECT_EQ (accounts.GetByName ("poster")->GetProto ()
                 .jobs_failed_as_poster (), 0);
}

/* ************************************************************************** */
/* Wanted board.                                                              */

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

TEST_F (WantedTests, NoAcceptNoFulfil)
{
  const auto id = PostBounty ();
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_FALSE (Process ("courier", R"({"f":)" + std::to_string (id) + "}"));
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
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (1u, 0u, 0u, 3000));
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
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (0u, 0u, 0u, 0));
  EXPECT_EQ (JobStats ("courier2"), std::make_tuple (0u, 0u, 0u, 0));
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
/* Protect / destroy / bodyguard (the entity-fate family).                    */

class EntityFateTests : public JobsTests
{

protected:

  /** Post terms for a protect job on building 1 (r=1000, co=500).  */
  const std::string protectPost =
      R"({"t":"protect","d":86400,"wd":86400,"r":1000,"co":500,"b":1})";

  /** Post terms for a destroy job on building 3 (r=1000, co=500).  */
  const std::string destroyPost =
      R"({"t":"destroy","d":86400,"wd":86400,"r":1000,"co":500,"b":3})";

};

TEST_F (EntityFateTests, ApprovalRequiredForAccept)
{
  ASSERT_TRUE (Process ("poster", protectPost));
  const auto id = OnlyJobId ();
  /* Nobody can accept until the poster designates them -- not even a
     would-be worker of the right faction.  */
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  ASSERT_TRUE (Process ("poster",
      R"({"s":)" + std::to_string (id) + R"(,"w":"courier"})"));
  EXPECT_FALSE (Process ("courier2", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
}

TEST_F (EntityFateTests, AncientTargetsRejected)
{
  EXPECT_FALSE (Process ("poster",
      R"({"t":"protect","d":86400,"wd":86400,"r":1000,"co":500,"b":2})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"destroy","d":86400,"wd":86400,"r":1000,"co":500,"b":2})"));
}

TEST_F (EntityFateTests, ProtectSuccessOnExpiry)
{
  PostAssignAccept (protectPost, "courier");
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  /* The building survived to the deadline: the worker collects.  */
  EXPECT_EQ (Balance ("courier"), 1000000 + 1000);
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (1u, 0u, 0u, 1000));
}

TEST_F (EntityFateTests, ProtectFailureOnDestruction)
{
  const auto id = PostAssignAccept (protectPost, "courier");
  OnJobEntityDestroyed (db, ctx, 1);
  EXPECT_FALSE (JobExists (id));
  /* The protected building died: reward refunds AND the bond forfeits, both
     to the poster; the failure is on the worker's record.  The poster could
     have destroyed their own building to harvest that bond, so the forfeit
     also marks THEIR record (the scam-vetting signal).  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 + 500);
  EXPECT_EQ (Balance ("courier"), 1000000 - 500);
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (0u, 1u, 0u, 0));
  EXPECT_EQ (JobStats ("poster"), std::make_tuple (0u, 0u, 1u, 0));
}

TEST_F (EntityFateTests, ProtectOpenUnwindsVoid)
{
  ASSERT_TRUE (Process ("poster", protectPost));
  const auto id = OnlyJobId ();
  OnJobEntityDestroyed (db, ctx, 1);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 10);
}

TEST_F (EntityFateTests, DestroyIsOpenAudienceAndSucceedsOnDeath)
{
  ASSERT_TRUE (Process ("poster", destroyPost));
  const auto id = OnlyJobId ();
  /* The audience is open: a cross-faction mercenary can be designated and
     accept.  */
  EXPECT_EQ (jobs.GetById (id)->GetFaction (), Faction::INVALID);
  ASSERT_TRUE (Process ("poster",
      R"({"s":)" + std::to_string (id) + R"(,"w":"green"})"));
  ASSERT_TRUE (Process ("green", R"({"a":)" + std::to_string (id) + "}"));

  OnJobEntityDestroyed (db, ctx, 3);
  EXPECT_FALSE (JobExists (id));
  /* The target died (by anyone's hand): the accepted worker collects.  */
  EXPECT_EQ (Balance ("green"), 1000000 + 1000);
  EXPECT_EQ (JobStats ("green"), std::make_tuple (1u, 0u, 0u, 1000));
}

TEST_F (EntityFateTests, DestroyFailsOnExpiry)
{
  PostAssignAccept (destroyPost, "courier");
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  /* The target still stands at the deadline: forfeit to the poster.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 + 500);
  EXPECT_EQ (Balance ("courier"), 1000000 - 500);
}

TEST_F (EntityFateTests, ProtectAndDestroySameBuildingSiege)
{
  /* Two opposing contracts on one building: the hook settles each row
     independently -- a siege market, not an interaction bug.  */
  const auto protectId = PostAssignAccept (protectPost, "courier");
  ASSERT_TRUE (Process ("green",
      R"({"t":"destroy","d":86400,"wd":86400,"r":1000,"co":500,"b":1})"));
  {
    /* The destroy row is the newest one in the table.  */
    auto res = jobs.QueryAll ();
    Database::IdT last = 0;
    while (res.Step ())
      last = jobs.GetFromResult (res)->GetId ();
    ASSERT_NE (last, protectId);
    ASSERT_TRUE (Process ("green",
        R"({"s":)" + std::to_string (last) + R"(,"w":"courier2"})"));
    ASSERT_TRUE (Process ("courier2",
        R"({"a":)" + std::to_string (last) + "}"));
  }

  OnJobEntityDestroyed (db, ctx, 1);

  /* Protect failed (forfeit to its poster); destroy succeeded (its worker
     collects).  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 + 500);
  EXPECT_EQ (Balance ("courier"), 1000000 - 500);
  EXPECT_EQ (Balance ("courier2"), 1000000 + 1000);
}

TEST_F (EntityFateTests, BodyguardLifecycle)
{
  const auto protectee = MakeCharacterAt ("poster", HexCoord (5, 5));

  /* Cannot bodyguard someone else's character.  */
  const auto other = MakeCharacterAt ("green", HexCoord (9, 9));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"bodyguard","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
      + std::to_string (other) + "}"));

  const auto id = PostAssignAccept (
      R"({"t":"bodyguard","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
      + std::to_string (protectee) + "}", "courier");
  EXPECT_EQ (jobs.GetById (id)->GetLinkedId (), protectee);

  /* The protectee dies inside the window: forfeit to the poster.  */
  OnJobEntityDestroyed (db, ctx, protectee);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 + 500);
  EXPECT_EQ (Balance ("courier"), 1000000 - 500);
}

TEST_F (EntityFateTests, BodyguardSuccessOnExpiry)
{
  const auto protectee = MakeCharacterAt ("poster", HexCoord (5, 5));
  PostAssignAccept (
      R"({"t":"bodyguard","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
      + std::to_string (protectee) + "}", "courier");
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_EQ (Balance ("courier"), 1000000 + 1000);
}

TEST_F (EntityFateTests, BodyguardProtecteeDockedInDestroyedBuildingFails)
{
  /* The protectee is docked inside a building that is then destroyed in
     combat.  The destruction deletes the character, which must still settle
     the bodyguard as a failure (forfeit to the poster) -- not leave a job
     dangling on a deleted character, and not pay the worker as if the
     protectee had survived to the deadline.  */
  const auto protectee = MakeCharacter ("poster", 1, {});
  const auto id = PostAssignAccept (
      R"({"t":"bodyguard","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
      + std::to_string (protectee) + "}", "courier");

  DestroyBuildingViaCombat (1);

  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 + 500);
  EXPECT_EQ (Balance ("courier"), 1000000 - 500);
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (0u, 1u, 0u, 0));
  EXPECT_EQ (JobStats ("poster"), std::make_tuple (0u, 0u, 1u, 0));
}

TEST_F (EntityFateTests, MultipleDockedProtecteesAllSettleOnDestruction)
{
  /* Several characters with independent bodyguard jobs are docked in one
     building: destroying it must settle every one of them, not just the
     first.  */
  const auto p1 = MakeCharacter ("poster", 1, {});
  const auto p2 = MakeCharacter ("poster", 1, {});
  const auto id1 = PostAssignAccept (
      R"({"t":"bodyguard","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
      + std::to_string (p1) + "}", "courier");
  const auto id2 = PostAssignAccept (
      R"({"t":"bodyguard","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
      + std::to_string (p2) + "}", "courier2");

  DestroyBuildingViaCombat (1);

  EXPECT_FALSE (JobExists (id1));
  EXPECT_FALSE (JobExists (id2));
  /* Both forfeited to the poster: 2 x collateral gained, 2 x fee burned.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 20 + 1000);
  EXPECT_EQ (Balance ("courier"), 1000000 - 500);
  EXPECT_EQ (Balance ("courier2"), 1000000 - 500);
}

/* ************************************************************************** */
/* Escort.                                                                    */

class EscortTests : public JobsTests
{

protected:

  Database::IdT protectee;

  EscortTests ()
  {
    protectee = MakeCharacterAt ("poster", HexCoord (5, 5));
  }

  /** The standard escort post: get the protectee to building 1.  */
  std::string
  EscortPost () const
  {
    return R"({"t":"escort","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
        + std::to_string (protectee) + R"(,"to":1})";
  }

};

TEST_F (EscortTests, PostRejects)
{
  /* Someone else's character; a bad destination.  */
  const auto other = MakeCharacterAt ("green", HexCoord (9, 9));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"escort","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
      + std::to_string (other) + R"(,"to":1})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"escort","d":86400,"wd":86400,"r":1000,"co":500,"ch":)"
      + std::to_string (protectee) + R"(,"to":3})"));
}

TEST_F (EscortTests, FulfilOnlyWhenProtecteeDockedAtDestination)
{
  const auto id = PostAssignAccept (EscortPost (), "courier");
  const std::string f = R"({"f":)" + std::to_string (id) + "}";

  /* Not there yet.  */
  EXPECT_FALSE (Process ("courier", f));

  /* Docked at the wrong building.  */
  characters.GetById (protectee)->SetBuildingId (2);
  EXPECT_FALSE (Process ("courier", f));

  /* Arrived: the worker collects.  */
  characters.GetById (protectee)->SetBuildingId (1);
  EXPECT_TRUE (Process ("courier", f));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 1000);
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (1u, 0u, 0u, 1000));
}

TEST_F (EscortTests, DeadProtecteeMakesItExpireAsFailure)
{
  const auto id = PostAssignAccept (EscortPost (), "courier");
  characters.DeleteById (protectee);
  EXPECT_FALSE (Process ("courier", R"({"f":)" + std::to_string (id) + "}"));

  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 + 500);
  EXPECT_EQ (Balance ("courier"), 1000000 - 500);
}

TEST_F (EscortTests, DestroyedDestinationVoids)
{
  const auto id = PostAssignAccept (EscortPost (), "courier");
  /* The linked destination (building 1) is razed by a third party: the
     worker can no longer deliver there through no fault of their own, so it
     voids -- reward back to the poster, collateral back to the worker, no
     failure mark (contrast a dead protectee, which IS the worker's fault).  */
  DestroyBuildingViaCombat (1);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 10);
  EXPECT_EQ (Balance ("courier"), 1000000);
  EXPECT_EQ (accounts.GetByName ("courier")->GetProto ().jobs_failed (), 0);
}

/* ************************************************************************** */
/* Patrol.                                                                    */

class PatrolTests : public JobsTests
{

protected:

  /** Patrol around (0,0) radius 10, 3 check-ins, 600s spacing.  */
  const std::string patrolPost =
      R"({"t":"patrol","d":86400,"wd":86400,"r":1000,"co":500,)"
      R"("x":0,"y":0,"rad":10,"k":3,"sp":600})";

};

TEST_F (PatrolTests, PostRejects)
{
  /* Zero radius / zero check-ins / an unschedulable plan ((k-1)*sp >= d).  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"patrol","d":86400,"wd":86400,"r":10,"co":0,"x":0,"y":0,"rad":0,"k":3,"sp":600})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"patrol","d":86400,"wd":86400,"r":10,"co":0,"x":0,"y":0,"rad":10,"k":0,"sp":600})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"patrol","d":86400,"wd":3600,"r":10,"co":0,"x":0,"y":0,"rad":10,"k":10,"sp":600})"));
  /* ...and it is the WORK window the schedule must fit (it runs from
     accept), not the listing window: the same schedule passes with a
     larger wd.  */
  EXPECT_TRUE (Process ("poster",
      R"({"t":"patrol","d":86400,"wd":7200,"r":10,"co":0,"x":0,"y":0,"rad":10,"k":10,"sp":600})"));
  /* A centre outside HexCoord's int16 range would be silently narrowed at
     check-in time -- it must be rejected at post.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"patrol","d":86400,"wd":86400,"r":10,"co":0,"x":100000,"y":0,"rad":10,"k":3,"sp":600})"));
  /* Integral-real centre / spacing (strict-integer convention).  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"patrol","d":86400,"wd":86400,"r":10,"co":0,"x":5.0,"y":0,"rad":10,"k":3,"sp":600})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"patrol","d":86400,"wd":86400,"r":10,"co":0,"x":0,"y":0,"rad":10,"k":3,"sp":600.0})"));
}

TEST_F (PatrolTests, CheckInsProgressAndComplete)
{
  CHECK (Process ("poster", patrolPost));
  const auto id = OnlyJobId ();
  CHECK (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  const auto ch = MakeCharacterAt ("courier", HexCoord (5, 0));
  const std::string f = R"({"f":)" + std::to_string (id) + R"(,"ch":)"
      + std::to_string (ch) + "}";

  ASSERT_TRUE (Process ("courier", f));
  EXPECT_EQ (jobs.GetById (id)->GetProto ().patrol ().checkins_done (), 1);

  /* Too soon: spacing enforced.  */
  ctx.SetTimestamp (BASE_TS + 300);
  EXPECT_FALSE (Process ("courier", f));

  ctx.SetTimestamp (BASE_TS + 600);
  ASSERT_TRUE (Process ("courier", f));

  ctx.SetTimestamp (BASE_TS + 1200);
  ASSERT_TRUE (Process ("courier", f));

  /* Third check-in completes and pays.  */
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 1000);
}

TEST_F (PatrolTests, CheckInRejects)
{
  CHECK (Process ("poster", patrolPost));
  const auto id = OnlyJobId ();
  CHECK (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  const std::string base = R"({"f":)" + std::to_string (id) + R"(,"ch":)";

  /* Outside the area.  */
  const auto far = MakeCharacterAt ("courier", HexCoord (50, 0));
  EXPECT_FALSE (Process ("courier", base + std::to_string (far) + "}"));

  /* A docked character is not patrolling.  */
  const auto docked = MakeCharacter ("courier", 1, {});
  EXPECT_FALSE (Process ("courier", base + std::to_string (docked) + "}"));

  /* Someone else's character does not count.  */
  const auto foreign = MakeCharacterAt ("courier2", HexCoord (5, 0));
  EXPECT_FALSE (Process ("courier", base + std::to_string (foreign) + "}"));

  /* A check-in without a character is malformed for patrol.  */
  EXPECT_FALSE (Process ("courier", R"({"f":)" + std::to_string (id) + "}"));
}

TEST_F (PatrolTests, ExpiryForfeitsPartialPatrol)
{
  CHECK (Process ("poster", patrolPost));
  const auto id = OnlyJobId ();
  CHECK (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  const auto ch = MakeCharacterAt ("courier", HexCoord (5, 0));
  ASSERT_TRUE (Process ("courier",
      R"({"f":)" + std::to_string (id) + R"(,"ch":)" + std::to_string (ch)
      + "}"));

  /* Only 1 of 3 check-ins done: all-or-nothing, the patrol forfeits.  */
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 + 500);
  EXPECT_EQ (Balance ("courier"), 1000000 - 500);
}

/* ************************************************************************** */
/* Rental.                                                                    */

class RentalTests : public JobsTests
{

protected:

  /**
   * Posts the standard rental: renter (poster) rents foo x5 from courier at
   * building 1; escrow 1000 of which rent 300 (deposit 700).
   */
  Database::IdT
  PostRental ()
  {
    CHECK (Process ("poster",
        R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":0,"rent":300,)"
        R"("i":"foo","n":5,"b":1,"w":"courier"})"));
    return OnlyJobId ();
  }

};

TEST_F (RentalTests, LifecycleCleanReturn)
{
  StageGoods (1, "courier", {{"foo", 5}});
  const auto id = PostRental ();

  /* The lessor is designated at post; only they may accept, and accepting
     hands the goods over.  */
  EXPECT_EQ (jobs.GetById (id)->GetProto ().designated_worker (), "courier");
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_EQ (ItemBalance (1, "courier", "foo"), 0);
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 5);

  /* The renter returns: goods back, rent to the lessor, deposit back.  */
  ASSERT_TRUE (Process ("poster", R"({"f":)" + std::to_string (id) + "}"));
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (ItemBalance (1, "courier", "foo"), 5);
  EXPECT_EQ (ItemBalance (1, "poster", "foo"), 0);
  /* Poster: -1000 escrow - 10 fee + 700 deposit back = -310.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 - 300);
  EXPECT_EQ (Balance ("courier"), 1000000 + 300);
  /* Both sides record the completion, but only the lessor EARNED the rent;
     the renter's value counter must not inflate with coins they paid.  */
  EXPECT_EQ (JobStats ("poster"), std::make_tuple (1u, 0u, 0u, 0));
  EXPECT_EQ (JobStats ("courier"), std::make_tuple (1u, 0u, 0u, 300));
}

TEST_F (RentalTests, PostRejects)
{
  StageGoods (1, "courier", {{"foo", 5}});
  /* rent > escrow; nonzero collateral; self-lessor; wrong-faction lessor;
     unknown item; foundation building.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":0,"rent":2000,"i":"foo","n":5,"b":1,"w":"courier"})"));
  /* Zero rent: a free rental would farm completion counters on both sides
     for just the posting fee.  */
  EXPECT_FALSE (Process ("poster",
      R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":0,"rent":0,"i":"foo","n":5,"b":1,"w":"courier"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":5,"rent":300,"i":"foo","n":5,"b":1,"w":"courier"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":0,"rent":300,"i":"foo","n":5,"b":1,"w":"poster"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":0,"rent":300,"i":"foo","n":5,"b":1,"w":"green"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":0,"rent":300,"i":"nope","n":5,"b":1,"w":"courier"})"));
  const auto fnd = MakeFoundation ("poster", Faction::RED);
  EXPECT_FALSE (Process ("poster",
      R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":0,"rent":300,"i":"foo","n":5,"b":)"
      + std::to_string (fnd) + R"(,"w":"courier"})"));
}

TEST_F (RentalTests, AcceptRequiresLessorStock)
{
  const auto id = PostRental ();
  /* The lessor holds nothing at the building: accept rejected.  */
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
  StageGoods (1, "courier", {{"foo", 5}});
  EXPECT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));
}

TEST_F (RentalTests, NonReturnDefaultsDeposit)
{
  StageGoods (1, "courier", {{"foo", 5}});
  const auto id = PostRental ();
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));

  /* The renter "loses" the goods (e.g. flies off with them and dies): at the
     deadline the whole escrow defaults to the lessor.  */
  {
    auto inv = buildingInv.Get (1, "poster");
    inv->GetInventory ().AddFungibleCount ("foo", -5);
  }
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 1000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 - 1000);
  /* The renter (= poster) is the party at fault, so this is a plain failure
     on their record — NOT a forfeit-as-poster mark (the lessor got paid,
     nobody forfeited a bond under this post).  */
  EXPECT_EQ (JobStats ("poster"), std::make_tuple (0u, 1u, 0u, 0));
}

TEST_F (RentalTests, ExpiryWithGoodsBackSettlesCleanly)
{
  StageGoods (1, "courier", {{"foo", 5}});
  const auto id = PostRental ();
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));

  /* The renter put the goods back but never sent the fulfil: the expiry
     check settles exactly like a clean return.  */
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (ItemBalance (1, "courier", "foo"), 5);
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 - 300);
  EXPECT_EQ (Balance ("courier"), 1000000 + 300);
}

TEST_F (RentalTests, HandoverBuildingDestroyedLeavesRentalLive)
{
  StageGoods (1, "courier", {{"foo", 5}});
  const auto id = PostRental ();
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));

  /* The renter takes the item away to use it -- the intended rental flow
     (swap the hull, fit the weapon).  The handover building's destruction
     proves nothing about the item's fate, so the rental must NOT settle:
     it stays live, escrow locked, until return or deadline.  */
  {
    auto inv = buildingInv.Get (1, "poster");
    inv->GetInventory ().AddFungibleCount ("foo", -5);
  }
  DestroyBuildingViaCombat (1);
  ASSERT_TRUE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 - 1000);
  EXPECT_EQ (Balance ("courier"), 1000000);

  /* At the deadline the count is not back at B (the empty inventory of a
     dead building must read as absent, not crash): a plain non-return --
     rent + deposit default to the lessor, the renter is at fault.  */
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 1000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 - 1000);
  EXPECT_EQ (JobStats ("poster"), std::make_tuple (0u, 1u, 0u, 0));
}

TEST_F (RentalTests, ZeroDepositNonReturnPaysLessorEverything)
{
  StageGoods (1, "courier", {{"foo", 5}});
  /* rent == escrow is valid: the deposit is simply zero.  */
  CHECK (Process ("poster",
      R"({"t":"rental","d":86400,"wd":86400,"r":1000,"co":0,"rent":1000,)"
      R"("i":"foo","n":5,"b":1,"w":"courier"})"));
  const auto id = OnlyJobId ();
  ASSERT_TRUE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));

  {
    auto inv = buildingInv.Get (1, "poster");
    inv->GetInventory ().AddFungibleCount ("foo", -5);
  }
  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("courier"), 1000000 + 1000);
  EXPECT_EQ (Balance ("poster"), 1000000 - 10 - 1000);
  EXPECT_EQ (JobStats ("poster"), std::make_tuple (0u, 1u, 0u, 0));
}

TEST_F (RentalTests, OpenRentalUnaffectedByBuildingDestruction)
{
  StageGoods (1, "courier", {{"foo", 5}});
  const auto id = PostRental ();

  /* An OPEN rental is not linked to anything: the handover building's
     destruction leaves it on the board.  The lessor can no longer accept
     (the stock check reads an empty inventory -- reject, not crash), and
     the escrow refunds as a normal void at the deadline.  */
  DestroyBuildingViaCombat (1);
  ASSERT_TRUE (JobExists (id));
  EXPECT_FALSE (Process ("courier", R"({"a":)" + std::to_string (id) + "}"));

  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 10);
  EXPECT_EQ (Balance ("courier"), 1000000);
}

/* ************************************************************************** */
/* Ad-slot.                                                                   */

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
  /* An ad rents a calendar window and takes no work window.  */
  EXPECT_FALSE (Process ("courier",
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
     the deadline, unlike the work-window types.  */
  const auto id = PostAd ();
  ctx.SetTimestamp (BASE_TS + 3600);
  ASSERT_TRUE (Process ("poster", R"({"a":)" + std::to_string (id) + "}"));
  EXPECT_EQ (jobs.GetById (id)->GetDeadline (), BASE_TS + DAY);
}

TEST_F (AdTests, AcceptRequiresMinimumWindowLeft)
{
  /* The payout is always the full rent, so the owner's approval must leave
     at least min_work_window of displayable time.  Exactly the floor is
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
     ExpireJobs, mirroring the due-job fulfil rule).  */
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
     building is sold; a transport job to the same building is unaffected.  */
  const auto accepted = PostAd ();
  ASSERT_TRUE (Process ("poster",
      R"({"a":)" + std::to_string (accepted) + "}"));
  CHECK (Process ("courier2",
      R"({"t":"ad","d":172800,"r":300,"co":0,"b":1,"slot":2,)"
      R"("hash":"def","start":86400})"));
  const auto open = LatestJobId ();
  CHECK (Process ("courier2",
      R"({"t":"transport","d":86400,"wd":86400,"r":2000,"co":8000,"to":1,)"
      R"("items":{"foo":5}})"));
  const auto transport = LatestJobId ();

  buildings.GetById (1)->SetOwner ("courier2");
  OnJobBuildingTransferred (db, ctx, 1);

  EXPECT_FALSE (JobExists (accepted));
  EXPECT_FALSE (JobExists (open));
  EXPECT_TRUE (JobExists (transport));
  /* The advertisers get their rent back (posting fees are burned); the old
     owner is not paid, and the transport reward stays escrowed.  */
  EXPECT_EQ (Balance ("courier"), 1000000 - 5);
  EXPECT_EQ (Balance ("courier2"), 1000000 - 3 - 2000 - 20);
  EXPECT_EQ (Balance ("poster"), 1000000);
}

/* ************************************************************************** */
/* Toll.                                                                      */

class TollTests : public JobsTests
{

protected:

  Database::IdT traveller;

  TollTests ()
  {
    traveller = MakeCharacterAt ("poster", HexCoord (5, 5));
  }

  /** poster pays green a toll of 500 for the traveller's safe passage.  */
  Database::IdT
  PostToll ()
  {
    CHECK (Process ("poster",
        R"({"t":"toll","d":86400,"wd":86400,"r":500,"co":0,"ch":)"
        + std::to_string (traveller) + R"(,"w":"green"})"));
    return OnlyJobId ();
  }

};

TEST_F (TollTests, LifecycleSurvivalPaysGatekeeper)
{
  const auto id = PostToll ();
  /* Cross-faction gatekeeper: the audience is open and green is designated.  */
  ASSERT_TRUE (Process ("green", R"({"a":)" + std::to_string (id) + "}"));

  ctx.SetTimestamp (BASE_TS + DAY + 1);
  ExpireJobs (db, ctx);
  EXPECT_FALSE (JobExists (id));
  /* The traveller survived the window: the gatekeeper collects.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 500 - 5);
  EXPECT_EQ (Balance ("green"), 1000000 + 500);
}

TEST_F (TollTests, TravellerDiesRefundsToll)
{
  const auto id = PostToll ();
  ASSERT_TRUE (Process ("green", R"({"a":)" + std::to_string (id) + "}"));
  OnJobEntityDestroyed (db, ctx, traveller);
  EXPECT_FALSE (JobExists (id));
  /* Kill the payer and the toll refunds: the gatekeeper gets nothing.  */
  EXPECT_EQ (Balance ("poster"), 1000000 - 5);
  EXPECT_EQ (Balance ("green"), 1000000);
}

TEST_F (TollTests, TravellerDockedInDestroyedBuildingRefunds)
{
  /* The traveller is docked inside a building that is then destroyed in
     combat: the toll must void and refund, exactly as an open-field death
     does -- the destruction must not read as the traveller surviving the
     window and pay the gatekeeper.  */
  const auto docked = MakeCharacter ("poster", 1, {});
  CHECK (Process ("poster",
      R"({"t":"toll","d":86400,"wd":86400,"r":500,"co":0,"ch":)"
      + std::to_string (docked) + R"(,"w":"green"})"));
  const auto id = OnlyJobId ();
  ASSERT_TRUE (Process ("green", R"({"a":)" + std::to_string (id) + "}"));

  DestroyBuildingViaCombat (1);

  EXPECT_FALSE (JobExists (id));
  EXPECT_EQ (Balance ("poster"), 1000000 - 5);
  EXPECT_EQ (Balance ("green"), 1000000);
}

TEST_F (TollTests, PostRejects)
{
  /* Someone else's character; self-gatekeeper; nonzero collateral.  */
  const auto other = MakeCharacterAt ("green", HexCoord (9, 9));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"toll","d":86400,"wd":86400,"r":500,"co":0,"ch":)"
      + std::to_string (other) + R"(,"w":"green"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"toll","d":86400,"wd":86400,"r":500,"co":0,"ch":)"
      + std::to_string (traveller) + R"(,"w":"poster"})"));
  EXPECT_FALSE (Process ("poster",
      R"({"t":"toll","d":86400,"wd":86400,"r":500,"co":5,"ch":)"
      + std::to_string (traveller) + R"(,"w":"green"})"));
}

} // anonymous namespace
} // namespace pxd
