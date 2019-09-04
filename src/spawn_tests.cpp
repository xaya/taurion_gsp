/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#include "testutils.hpp"

#include "database/dbtest.hpp"
#include "hexagonal/coord.hpp"
#include "hexagonal/ring.hpp"
#include "mapdata/basemap.hpp"

#include <gtest/gtest.h>

#include <unordered_set>

namespace pxd
{
namespace
{

class SpawnTests : public DBTestWithSchema
{

private:

  /** Test random instance.  */
  TestRandom rnd;

protected:

  /** Params instance for testing.  */
  const Params params;

  /** Basemap instance for testing.  */
  const BaseMap map;

  /** Dynamic obstacle instance for testing.  */
  DynObstacles dyn;

  /** Character table for tests.  */
  CharacterTable tbl;

  SpawnTests ()
    : params(xaya::Chain::MAIN), dyn(db), tbl(db)
  {}

  /**
   * Spawns a character with the test references needed for that.
   */
  CharacterTable::Handle
  Spawn (const std::string& owner, const Faction f)
  {
    return SpawnCharacter (owner, f, tbl, dyn, rnd, map, params);
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

  auto c = tbl.GetById (1);
  ASSERT_TRUE (c != nullptr);
  ASSERT_EQ (c->GetOwner (), "domob");

  EXPECT_TRUE (c->GetProto ().has_combat_data ());
  EXPECT_EQ (c->GetProto ().combat_data ().attacks_size (), 2);
}

class SpawnLocationTests : public SpawnTests
{

protected:

  /**
   * Spawns a character and removes it again from the map.  This just
   * returns the spawn location.
   */
  HexCoord
  SpawnLocation (const Faction f)
  {
    auto c = Spawn ("domob", f);
    const auto id = c->GetId ();
    const HexCoord res = c->GetPosition ();
    c.reset ();

    dyn.RemoveVehicle (res, f);
    tbl.DeleteById (id);

    return res;
  }

};

TEST_F (SpawnLocationTests, SpawnLocation)
{
  /* Faction red has a spawn area without obstacles.  This simplifies things
     a lot for this test, as we do not have to consider displaced spawns.  */
  constexpr Faction f = Faction::RED;

  HexCoord::IntT spawnRadius;
  const HexCoord spawnCentre = params.SpawnArea (f, spawnRadius);

  /* In this test, we randomly spawn (and then remove again) many characters
     on the map.  We expect that all are within the spawn radius of the centre
     (since there are no obstacles on the ring boundary).  We also expect to
     find at least some with the maximum distance, and some within some "small"
     distance (as looking for the exact centre has a low probability).  */
  constexpr unsigned trials = 1000;
  constexpr HexCoord::IntT smallDist = 10;

  unsigned foundOuter = 0;
  unsigned foundInner = 0;
  for (unsigned i = 0; i < trials; ++i)
    {
      const auto pos = SpawnLocation (f);
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

TEST_F (SpawnLocationTests, WithObstacles)
{
  /* Faction blue has a significant amount of obstacles in the spawning area,
     including on the ring boundary.  Thus we can use it to test that map
     obstacles are fine.  */
  constexpr Faction f = Faction::BLUE;

  HexCoord::IntT spawnRadius;
  const HexCoord spawnCentre = params.SpawnArea (f, spawnRadius);

  /* We spawn a couple of characters (and remove them again) and expect
     that some of them are actually placed outside the spawn radius; namely
     because they got placed on an obstacle and had to be displaced to
     outside the map.  We also expect that all locations are passable.  */
  constexpr unsigned trials = 1000;

  unsigned outside = 0;
  for (unsigned i = 0; i < trials; ++i)
    {
      const auto pos = SpawnLocation (f);
      ASSERT_TRUE (map.IsPassable (pos));

      const auto dist = HexCoord::DistanceL1 (pos, spawnCentre);
      if (dist > spawnRadius)
        ++outside;
    }

  LOG (INFO) << outside << " locations were displaced to outside the ring";
  EXPECT_GT (outside, 0);
}

TEST_F (SpawnLocationTests, DynObstacles)
{
  /* Faction red has no obstacles on the basemap in the spawn area, so by
     choosing this faction, we will be able to really fill up the entire
     area of the spawn with vehicles.  */
  constexpr Faction f = Faction::RED;

  HexCoord::IntT spawnRadius;
  const HexCoord spawnCentre = params.SpawnArea (f, spawnRadius);

  /* The 50x50 spawn area has less than 10k tiles.  So if we create 10k
     characters, some will be displaced out of the spawn area.  It should
     still work fine.  In the end, we should get all locations in the spawn
     area filled up, and we should not get any vehicle on top of another.  */
  constexpr unsigned vehicles = 10000;

  unsigned outside = 0;
  std::unordered_set<HexCoord> positions;
  for (unsigned i = 0; i < vehicles; ++i)
    {
      auto c = Spawn ("domob", f);
      const auto& pos = c->GetPosition ();

      if (HexCoord::DistanceL1 (pos, spawnCentre) > spawnRadius)
        ++outside;

      const auto res = positions.insert (pos);
      ASSERT_TRUE (res.second);
    }
  ASSERT_EQ (positions.size (), vehicles);
  LOG (INFO) << "Vehicles outside of spawn ring: " << outside;

  unsigned tilesInside = 0;
  for (HexCoord::IntT r = 0; r <= spawnRadius; ++r)
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

} // anonymous namespace
} // namespace pxd
