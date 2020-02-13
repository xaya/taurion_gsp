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

#include "database/character.hpp"
#include "database/dbtest.hpp"

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

  BuildingsTests ()
    : tbl(db)
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

/* ************************************************************************** */

class ProcessEnterBuildingsTests : public BuildingsTests
{

protected:

  CharacterTable characters;

  ProcessEnterBuildingsTests ()
    : characters(db)
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
  c->SetBusy (2);
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
  c->MutableProto ().mutable_target ();
  c->MutableProto ().mutable_movement ()->mutable_waypoints ();
  c->MutableProto ().mutable_mining ()->set_active (true);
  c.reset ();

  ProcessEnterBuildings (db);

  c = GetCharacter (10);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (c->GetBuildingId (), 1);
  EXPECT_EQ (c->GetEnterBuilding (), Database::EMPTY_ID);
  EXPECT_FALSE (c->GetProto ().has_target ());
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

} // anonymous namespace
} // namespace pxd
