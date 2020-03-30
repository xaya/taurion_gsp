/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "buildings.hpp"

#include "testutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "hexagonal/ring.hpp"
#include "proto/roconfig.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <glog/logging.h>

namespace pxd
{
namespace
{

using testing::UnorderedElementsAre;

/* ************************************************************************** */

class BuildingsTests : public DBTestWithSchema
{

protected:

  BuildingsTable tbl;
  CharacterTable characters;

  BuildingsTests ()
    : tbl(db), characters(db)
  {}

};

TEST_F (BuildingsTests, GetBuildingShape)
{
  auto h = tbl.CreateNew ("checkmark", "", Faction::ANCIENT);
  const auto id1 = h->GetId ();
  h->SetCentre (HexCoord (-1, 5));
  h->MutableProto ().mutable_shape_trafo ()->set_rotation_steps (2);
  h.reset ();

  const auto id2 = tbl.CreateNew ("invalid", "", Faction::ANCIENT)->GetId ();

  EXPECT_THAT (GetBuildingShape (*tbl.GetById (id1)),
               UnorderedElementsAre (HexCoord (-1, 5),
                                     HexCoord (-1, 4),
                                     HexCoord (0, 4),
                                     HexCoord (1, 3)));
  EXPECT_DEATH (GetBuildingShape (*tbl.GetById (id2)), "undefined type");
}

TEST_F (BuildingsTests, UpdateBuildingStats)
{
  auto h = tbl.CreateNew ("r_rt", "domob", Faction::RED);
  UpdateBuildingStats (*h);
  EXPECT_EQ (h->GetProto ().combat_data ().attacks_size (), 1);
  EXPECT_GT (h->GetRegenData ().max_hp ().armour (), 0);
  EXPECT_GT (h->GetHP ().armour (), 0);
}

/* ************************************************************************** */

class ProcessEnterBuildingsTests : public BuildingsTests
{

protected:

  ProcessEnterBuildingsTests ()
  {
    auto b = tbl.CreateNew ("checkmark", "", Faction::ANCIENT);
    CHECK_EQ (b->GetId (), 1);
    b->SetCentre (HexCoord (0, 0));
    b.reset ();
  }

  /**
   * Creates or looks up a test character with the given ID.
   */
  CharacterTable::Handle
  GetCharacter (const Database::IdT id)
  {
    auto h = characters.GetById (id);
    if (h == nullptr)
      {
        db.SetNextId (id);
        h = characters.CreateNew ("domob", Faction::RED);
      }

    return h;
  }

};

TEST_F (ProcessEnterBuildingsTests, BusyCharacter)
{
  auto c = GetCharacter (10);
  c->SetPosition (HexCoord (5, 0));
  c->SetEnterBuilding (1);
  c->MutableProto ().set_ongoing (12345);
  c.reset ();

  ProcessEnterBuildings (db);

  c = GetCharacter (10);
  ASSERT_FALSE (c->IsInBuilding ());
  EXPECT_EQ (c->GetEnterBuilding (), 1);
}

TEST_F (ProcessEnterBuildingsTests, NonExistantBuilding)
{
  auto c = GetCharacter (10);
  c->SetPosition (HexCoord (5, 0));
  c->SetEnterBuilding (42);
  c.reset ();

  ProcessEnterBuildings (db);

  c = GetCharacter (10);
  ASSERT_FALSE (c->IsInBuilding ());
  EXPECT_EQ (c->GetEnterBuilding (), Database::EMPTY_ID);
}

TEST_F (ProcessEnterBuildingsTests, TooFar)
{
  auto c = GetCharacter (10);
  c->SetPosition (HexCoord (6, 0));
  c->SetEnterBuilding (1);
  c.reset ();

  ProcessEnterBuildings (db);

  c = GetCharacter (10);
  ASSERT_FALSE (c->IsInBuilding ());
  EXPECT_EQ (c->GetEnterBuilding (), 1);
}

TEST_F (ProcessEnterBuildingsTests, EnteringEffects)
{
  auto c = GetCharacter (10);
  c->SetPosition (HexCoord (5, 0));
  c->SetEnterBuilding (1);
  proto::TargetId t;
  t.set_id (42);
  c->SetTarget (t);
  c->MutableProto ().mutable_movement ()->mutable_waypoints ();
  c->MutableProto ().mutable_mining ()->set_active (true);
  c.reset ();

  ProcessEnterBuildings (db);

  c = GetCharacter (10);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (c->GetBuildingId (), 1);
  EXPECT_EQ (c->GetEnterBuilding (), Database::EMPTY_ID);
  EXPECT_FALSE (c->HasTarget ());
  EXPECT_FALSE (c->GetProto ().has_movement ());
  EXPECT_FALSE (c->GetProto ().mining ().active ());
}

TEST_F (ProcessEnterBuildingsTests, MultipleCharacters)
{
  auto c = GetCharacter (10);
  c->SetPosition (HexCoord (4, 0));

  c = GetCharacter (11);
  c->SetPosition (HexCoord (6, 0));
  c->SetEnterBuilding (1);

  c = GetCharacter (12);
  c->SetPosition (HexCoord (5, 0));
  c->SetEnterBuilding (1);

  c = GetCharacter (13);
  c->SetPosition (HexCoord (2, 0));
  c->SetEnterBuilding (1);
  c.reset ();

  ProcessEnterBuildings (db);

  EXPECT_FALSE (GetCharacter (10)->IsInBuilding ());
  EXPECT_FALSE (GetCharacter (11)->IsInBuilding ());
  EXPECT_TRUE (GetCharacter (12)->IsInBuilding ());
  EXPECT_TRUE (GetCharacter (13)->IsInBuilding ());
}

/* ************************************************************************** */

class LeaveBuildingTests : public BuildingsTests
{

protected:

  const HexCoord centre;
  HexCoord::IntT radius;

  TestRandom rnd;
  DynObstacles dyn;
  ContextForTesting ctx;

  LeaveBuildingTests ()
    : centre(10, 42), dyn(db)
  {
    const std::string type = "checkmark";
    radius = RoConfigData ().building_types ().at (type).enter_radius ();

    auto b = tbl.CreateNew (type, "", Faction::ANCIENT);
    CHECK_EQ (b->GetId (), 1);
    b->SetCentre (centre);
    dyn.AddBuilding (*b);
    b.reset ();

    db.SetNextId (10);
    auto c = characters.CreateNew ("domob", Faction::RED);
    c->SetBuildingId (1);
    c.reset ();
  }

  /**
   * Calls LeaveBuilding on our test character with all our other context
   * and returns the resulting position.
   */
  HexCoord
  Leave ()
  {
    auto c = characters.GetById (10);
    LeaveBuilding (tbl, *c, rnd, dyn, ctx);
    CHECK (!c->IsInBuilding ());
    return c->GetPosition ();
  }

};

TEST_F (LeaveBuildingTests, Basic)
{
  const auto pos = Leave ();
  EXPECT_TRUE (ctx.Map ().IsPassable (pos));
  EXPECT_TRUE (dyn.IsPassable (pos, Faction::RED));
  EXPECT_LE (HexCoord::DistanceL1 (pos, centre), radius);
}

TEST_F (LeaveBuildingTests, WhenAllBlocked)
{
  for (HexCoord::IntT r = 0; r <= radius; ++r)
    for (const auto& c : L1Ring (centre, r))
      dyn.AddVehicle (c, Faction::RED);

  const auto pos = Leave ();
  EXPECT_TRUE (ctx.Map ().IsPassable (pos));
  EXPECT_TRUE (dyn.IsPassable (pos, Faction::RED));
  EXPECT_GT (HexCoord::DistanceL1 (pos, centre), radius);
}

TEST_F (LeaveBuildingTests, PossibleLocations)
{
  constexpr unsigned trials = 1'000;

  /* We use a map that contains all tiles within radius.  They count how
     often a certain location is chosen as final position.  We set the
     tiles that are unpassable (because the building is there) to -1
     instead to signal this.  */
  std::map<HexCoord, int> counts;
  for (HexCoord::IntT r = 0; r <= radius; ++r)
    for (const auto& c : L1Ring (centre, r))
      counts.emplace (c, 0);
  for (const auto& c : GetBuildingShape (*tbl.GetById (1)))
    counts.at (c) = -1;

  for (unsigned i = 0; i < trials; ++i)
    {
      const auto pos = Leave ();
      characters.GetById (10)->SetBuildingId (1);

      auto mit = counts.find (pos);
      ASSERT_NE (mit, counts.end ()) << "Left to unexpected position: " << pos;
      ASSERT_GE (mit->second, 0) << "Left to obstacle: " << pos;
      ++mit->second;
    }

  for (const auto& entry : counts)
    {
      if (entry.second == -1)
        continue;
      LOG (INFO) << "Count at " << entry.first << ": " << entry.second;
      EXPECT_GE (entry.second, 3);
    }
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
