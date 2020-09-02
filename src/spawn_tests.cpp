/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#include "spawn.hpp"

#include "buildings.hpp"
#include "protoutils.hpp"
#include "testutils.hpp"

#include "database/dbtest.hpp"
#include "hexagonal/coord.hpp"
#include "hexagonal/ring.hpp"

#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>

#include <unordered_set>

namespace pxd
{
namespace
{

using google::protobuf::util::MessageDifferencer;

/* ************************************************************************** */

class SpawnTests : public DBTestWithSchema
{

protected:

  TestRandom rnd;
  ContextForTesting ctx;

  DynObstacles dyn;
  CharacterTable tbl;

  SpawnTests ()
    : dyn(db, ctx), tbl(db)
  {
    InitialiseBuildings (db, ctx.Chain ());
    db.SetNextId (1'001);
  }

  /**
   * Spawns a character with the test references needed for that.
   */
  CharacterTable::Handle
  Spawn (const std::string& owner, const Faction f)
  {
    return SpawnCharacter (owner, f, tbl, dyn, rnd, ctx);
  }

};

TEST_F (SpawnTests, Basic)
{
  Spawn ("domob", Faction::RED);
  Spawn ("domob", Faction::GREEN);
  Spawn ("andy", Faction::BLUE);

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::RED);

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::GREEN);

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::BLUE);

  EXPECT_FALSE (res.Step ());
}

TEST_F (SpawnTests, DataInitialised)
{
  Spawn ("domob", Faction::RED);

  auto c = tbl.GetById (1'001);
  ASSERT_TRUE (c != nullptr);
  ASSERT_EQ (c->GetOwner (), "domob");

  EXPECT_TRUE (c->GetProto ().has_combat_data ());
  EXPECT_GT (c->GetProto ().cargo_space (), 0);
  EXPECT_GT (c->GetHP ().armour (), 0);
  EXPECT_GT (c->GetHP ().shield (), 0);
  EXPECT_TRUE (MessageDifferencer::Equals (c->GetHP (),
                                           c->GetRegenData ().max_hp ()));
}

TEST_F (SpawnTests, SpawnOnMap)
{
  ctx.SetHeight (499);
  ASSERT_FALSE (ctx.Forks ().IsActive (Fork::UnblockSpawns));

  auto c = Spawn ("domob", Faction::RED);
  EXPECT_FALSE (c->IsInBuilding ());
}

TEST_F (SpawnTests, SpawnIntoBuildings)
{
  BuildingsTable buildings(db);

  ctx.SetHeight (500);
  ASSERT_TRUE (ctx.Forks ().IsActive (Fork::UnblockSpawns));

  auto c = Spawn ("domob", Faction::RED);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (buildings.GetById (c->GetBuildingId ())->GetType (), "r ss");

  c = Spawn ("domob", Faction::GREEN);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (buildings.GetById (c->GetBuildingId ())->GetType (), "g ss");

  c = Spawn ("domob", Faction::BLUE);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (buildings.GetById (c->GetBuildingId ())->GetType (), "b ss");
}

/* ************************************************************************** */

class SpawnLocationTests : public SpawnTests
{

protected:

  /**
   * Chooses a spawn location for the given centre and radius (and all
   * other context from the test fixture).
   */
  HexCoord
  SpawnLocation (const HexCoord& centre, const HexCoord::IntT radius,
                 const Faction f)
  {
    return ChooseSpawnLocation (centre, radius, f, rnd, dyn, ctx);
  }

};

TEST_F (SpawnLocationTests, NoObstaclesInSpawns)
{
  for (const Faction f : {Faction::RED, Faction::GREEN, Faction::BLUE})
    {
      const auto& spawn
          = ctx.RoConfig ()->params ().spawn_areas ().at (FactionToString (f));
      const auto spawnCentre = CoordFromProto (spawn.centre ());

      for (unsigned r = 0; r <= spawn.radius (); ++r)
        {
          const L1Ring ring(spawnCentre, r);
          for (const auto& pos : ring)
            ASSERT_TRUE (ctx.Map ().IsPassable (pos))
                << "Tile " << pos << " for faction " << FactionToString (f)
                << " is not passable";
        }
    }
}

TEST_F (SpawnLocationTests, SpawnLocation)
{
  constexpr Faction f = Faction::RED;
  constexpr HexCoord::IntT spawnRadius = 20;
  const HexCoord spawnCentre = HexCoord (42, -10);

  /* In this test, we randomly choose spawn locations (without adding
     actual characters there).  We expect that all are within the spawn radius
     of the centre (since there are no obstacles on the ring boundary).
     We also expect to find at least some with the maximum distance, and some
     within some "small" distance (as looking for the exact centre has a low
     probability).  */
  constexpr unsigned trials = 1000;
  constexpr HexCoord::IntT smallDist = 5;

  unsigned foundOuter = 0;
  unsigned foundInner = 0;
  for (unsigned i = 0; i < trials; ++i)
    {
      const auto pos = SpawnLocation (spawnCentre, spawnRadius, f);
      const auto dist = HexCoord::DistanceL1 (pos, spawnCentre);

      ASSERT_LE (dist, spawnRadius);
      if (dist == spawnRadius)
        ++foundOuter;
      else if (dist <= smallDist)
        ++foundInner;
    }

  LOG (INFO) << "Found " << foundOuter << " positions with max distance";
  LOG (INFO) << "Found " << foundInner << " positions within " << smallDist;
  EXPECT_GT (foundOuter, 0);
  EXPECT_GT (foundInner, 0);
}

TEST_F (SpawnLocationTests, DynObstacles)
{
  const Faction f = Faction::RED;
  const auto& spawn
      = ctx.RoConfig ()->params ().spawn_areas ().at (FactionToString (f));
  const HexCoord spawnCentre = CoordFromProto (spawn.centre ());

  /* The 50x50 spawn area has less than 10k tiles.  So if we create 10k
     characters, some will be displaced out of the spawn area.  It should
     still work fine.  In the end, we should get all locations in the spawn
     area filled up, and we should not get any vehicle on top of another.  */
  constexpr unsigned vehicles = 10'000;

  unsigned outside = 0;
  std::unordered_set<HexCoord> positions;
  for (unsigned i = 0; i < vehicles; ++i)
    {
      auto c = Spawn ("domob", f);
      const auto& pos = c->GetPosition ();

      const unsigned dist = HexCoord::DistanceL1 (pos, spawnCentre);
      if (dist > spawn.radius ())
        ++outside;

      const auto res = positions.insert (pos);
      ASSERT_TRUE (res.second);
    }
  ASSERT_EQ (positions.size (), vehicles);
  LOG (INFO) << "Vehicles outside of spawn ring: " << outside;

  unsigned tilesInside = 0;
  for (unsigned r = 0; r <= spawn.radius (); ++r)
    {
      const L1Ring ring(spawnCentre, r);
      for (const auto& pos : ring)
        {
          ++tilesInside;
          ASSERT_EQ (positions.count (pos), 1);
        }
    }
  LOG (INFO) << "Tiles inside spawn ring: " << tilesInside;
  EXPECT_EQ (tilesInside + outside, vehicles);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
