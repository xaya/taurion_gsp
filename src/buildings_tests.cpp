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

  ContextForTesting ctx;

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

  EXPECT_THAT (GetBuildingShape (*tbl.GetById (id1), ctx),
               UnorderedElementsAre (HexCoord (-1, 5),
                                     HexCoord (-1, 4),
                                     HexCoord (0, 4),
                                     HexCoord (1, 3)));
  EXPECT_DEATH (GetBuildingShape (*tbl.GetById (id2), ctx), "Unknown building");
}

TEST_F (BuildingsTests, UpdateBuildingStats)
{
  auto h = tbl.CreateNew ("r_rt", "domob", Faction::RED);
  UpdateBuildingStats (*h, ctx.Chain ());
  EXPECT_EQ (h->GetProto ().combat_data ().attacks_size (), 1);
  EXPECT_EQ (h->GetRegenData ().max_hp ().armour (), 1'000);
  EXPECT_EQ (h->GetHP ().armour (), 1'000);

  h->MutableProto ().set_foundation (true);
  UpdateBuildingStats (*h, ctx.Chain ());
  EXPECT_EQ (h->GetProto ().combat_data ().attacks_size (), 0);
  EXPECT_EQ (h->GetRegenData ().max_hp ().armour (), 50);
  EXPECT_EQ (h->GetHP ().armour (), 50);
}

/* ************************************************************************** */

class CanPlaceBuildingTests : public BuildingsTests
{

protected:

  CanPlaceBuildingTests ()
  {}

  /**
   * Calls CanPlaceBuilding with fresh dynobstacles map from the database and a
   * shape trafo that contains the given rotation.
   */
  bool
  CanPlace (const std::string& type, const unsigned rot,
            const HexCoord& pos)
  {
    DynObstacles dyn(db, ctx);
    proto::ShapeTransformation trafo;
    trafo.set_rotation_steps (rot);
    return CanPlaceBuilding (type, trafo, pos, dyn, ctx);
  }

};

TEST_F (CanPlaceBuildingTests, Ok)
{
  /* Some offset added to all coordinates to make the situation fit
     into one region entirely.  */
  const HexCoord offs(-1, -5);

  tbl.CreateNew ("huesli", "", Faction::ANCIENT)
      ->SetCentre (offs + HexCoord (-1, 0));

  characters.CreateNew ("domob", Faction::RED)
      ->SetPosition (offs + HexCoord (2, 0));
  characters.CreateNew ("andy", Faction::GREEN)
      ->SetPosition (offs + HexCoord (0, -1));
  characters.CreateNew ("daniel", Faction::BLUE)
      ->SetPosition (offs + HexCoord (0, 3));

  EXPECT_TRUE (CanPlace ("checkmark", 0, offs));
}

TEST_F (CanPlaceBuildingTests, OutOfMap)
{
  EXPECT_FALSE (CanPlace ("huesli", 0, HexCoord (10'000, 0)));
}

TEST_F (CanPlaceBuildingTests, Impassable)
{
  const HexCoord impassable(149, 0);
  ASSERT_FALSE (ctx.Map ().IsPassable (impassable));

  EXPECT_FALSE (CanPlace ("huesli", 0, impassable));
}

TEST_F (CanPlaceBuildingTests, DynObstacle)
{
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 0));
  EXPECT_FALSE (CanPlace ("huesli", 0, HexCoord (0, 0)));
}

TEST_F (CanPlaceBuildingTests, MultiRegion)
{
  const HexCoord pos(0, 0);
  const HexCoord outside(pos + HexCoord (0, 2));
  ASSERT_NE (ctx.Map ().Regions ().GetRegionId (pos),
             ctx.Map ().Regions ().GetRegionId (outside));

  EXPECT_FALSE (CanPlace ("checkmark", 0, pos));
}

/* ************************************************************************** */

class MaybeStartBuildingConstructionTests : public BuildingsTests
{

protected:

  OngoingsTable ongoings;

  /** An example "huesli" building handle for use in this test.  */
  BuildingsTable::Handle huesli;

  /** The huesli building's construction inventory.  */
  Inventory cInv;

  MaybeStartBuildingConstructionTests ()
    : ongoings(db),
      huesli(tbl.CreateNew ("huesli", "domob", Faction::RED)),
      cInv(*huesli->MutableProto ().mutable_construction_inventory ())
  {
    huesli->MutableProto ().set_foundation (true);
    ctx.SetHeight (100);

    db.SetNextId (101);
  }

};

TEST_F (MaybeStartBuildingConstructionTests, NotEnoughResources)
{
  cInv.AddFungibleCount ("foo", 2);
  cInv.AddFungibleCount ("zerospace", 100);

  MaybeStartBuildingConstruction (*huesli, ongoings, ctx);
  EXPECT_FALSE (ongoings.QueryAll ().Step ());
  EXPECT_FALSE (huesli->GetProto ().has_ongoing_construction ());
}

TEST_F (MaybeStartBuildingConstructionTests, AlreadyConstructing)
{
  huesli->MutableProto ().set_ongoing_construction (42);

  cInv.AddFungibleCount ("foo", 3);
  cInv.AddFungibleCount ("zerospace", 100);

  MaybeStartBuildingConstruction (*huesli, ongoings, ctx);
  EXPECT_FALSE (ongoings.QueryAll ().Step ());
}

TEST_F (MaybeStartBuildingConstructionTests, StartsOperation)
{
  cInv.AddFungibleCount ("foo", 3);
  cInv.AddFungibleCount ("zerospace", 100);

  MaybeStartBuildingConstruction (*huesli, ongoings, ctx);

  auto res = ongoings.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto op = ongoings.GetFromResult (res);
  EXPECT_EQ (op->GetHeight (), 110);
  EXPECT_EQ (op->GetBuildingId (), huesli->GetId ());
  EXPECT_TRUE (op->GetProto ().has_building_construction ());
  EXPECT_EQ (huesli->GetProto ().ongoing_construction (), op->GetId ());

  EXPECT_FALSE (res.Step ());
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

  /**
   * Processes the entering with a custom, local DynObstacles instance.
   */
  void
  ProcessEnter ()
  {
    DynObstacles dyn(db, ctx);
    ProcessEnter (dyn);
  }

  /**
   * Processes the entering using the given DynObstacles instance.  This
   * allows us to check the updates to it.
   */
  void
  ProcessEnter (DynObstacles& dyn)
  {
    ProcessEnterBuildings (db, dyn, ctx);
  }

};

TEST_F (ProcessEnterBuildingsTests, BusyCharacter)
{
  auto c = GetCharacter (10);
  c->SetPosition (HexCoord (5, 0));
  c->SetEnterBuilding (1);
  c->MutableProto ().set_ongoing (12345);
  c.reset ();

  ProcessEnter ();

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

  ProcessEnter ();

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

  ProcessEnter ();

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

  DynObstacles dyn(db, ctx);
  ASSERT_FALSE (dyn.IsPassable (HexCoord (5, 0), Faction::RED));

  ProcessEnter (dyn);

  c = GetCharacter (10);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (c->GetBuildingId (), 1);
  EXPECT_EQ (c->GetEnterBuilding (), Database::EMPTY_ID);
  EXPECT_FALSE (c->HasTarget ());
  EXPECT_FALSE (c->GetProto ().has_movement ());
  EXPECT_FALSE (c->GetProto ().mining ().active ());
  EXPECT_TRUE (dyn.IsPassable (HexCoord (5, 0), Faction::RED));
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

  ProcessEnter ();

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

  LeaveBuildingTests ()
    : centre(10, 42)
  {
    const std::string type = "checkmark";
    radius = ctx.RoConfig ().Building (type).enter_radius ();

    auto b = tbl.CreateNew (type, "", Faction::ANCIENT);
    CHECK_EQ (b->GetId (), 1);
    b->SetCentre (centre);
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
    DynObstacles dyn(db, ctx);
    return Leave (dyn);
  }

  /**
   * Calls LeaveBuilding, using the existing DynObstacles instance so we
   * can verify the effect on it.
   */
  HexCoord
  Leave (DynObstacles& dyn)
  {
    auto c = characters.GetById (10);
    LeaveBuilding (tbl, *c, rnd, dyn, ctx);
    CHECK (!c->IsInBuilding ());
    return c->GetPosition ();
  }

};

TEST_F (LeaveBuildingTests, Basic)
{
  DynObstacles originalDyn(db, ctx);
  DynObstacles dyn(db, ctx);
  const auto pos = Leave (dyn);
  EXPECT_TRUE (ctx.Map ().IsPassable (pos));
  EXPECT_TRUE (originalDyn.IsPassable (pos, Faction::RED));
  EXPECT_FALSE (dyn.IsPassable (pos, Faction::RED));
  EXPECT_LE (HexCoord::DistanceL1 (pos, centre), radius);
}

TEST_F (LeaveBuildingTests, WhenAllBlocked)
{
  for (HexCoord::IntT r = 0; r <= radius; ++r)
    for (const auto& c : L1Ring (centre, r))
      characters.CreateNew ("domob", Faction::RED)->SetPosition (c);
  DynObstacles originalDyn(db, ctx);

  const auto pos = Leave ();
  EXPECT_TRUE (ctx.Map ().IsPassable (pos));
  EXPECT_TRUE (originalDyn.IsPassable (pos, Faction::RED));
  EXPECT_GT (HexCoord::DistanceL1 (pos, centre), radius);
}

TEST_F (LeaveBuildingTests, FillingAreaUp)
{
  std::vector<Database::IdT> ids;
  for (unsigned i = 0; i < 1'000; ++i)
    {
      auto c = characters.CreateNew ("domob", Faction::RED);
      c->SetBuildingId (1);
      ids.push_back (c->GetId ());
    }
  DynObstacles dyn(db, ctx);

  /* If we leave with all the characters, it will fill up the general
     area around the building.  All should still work fine, including update
     to the DynObstacles instance (preventing characters on top of each
     other in the end).  */

  std::set<HexCoord> positions;
  for (const auto id : ids)
    {
      auto c = characters.GetById (id);
      LeaveBuilding (tbl, *c, rnd, dyn, ctx);
      positions.insert (c->GetPosition ());
    }
  EXPECT_EQ (positions.size (), ids.size ());
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
  for (const auto& c : GetBuildingShape (*tbl.GetById (1), ctx))
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
