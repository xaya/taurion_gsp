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

#include "dynobstacles.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class DynObstaclesTests : public DBTestWithSchema
{

protected:

  BuildingsTable buildings;
  CharacterTable characters;

  DynObstaclesTests ()
    : buildings(db), characters(db)
  {}

};

TEST_F (DynObstaclesTests, VehiclesFromDb)
{
  const HexCoord c1(2, 5);
  const HexCoord c2(-1, 7);
  const HexCoord c3(0, 0);
  characters.CreateNew ("domob", Faction::RED)->SetPosition (c1);
  characters.CreateNew ("domob", Faction::GREEN)->SetPosition (c1);
  characters.CreateNew ("domob", Faction::BLUE)->SetPosition (c2);

  DynObstacles dyn(db);

  EXPECT_FALSE (dyn.IsPassable (c1, Faction::RED));
  EXPECT_FALSE (dyn.IsPassable (c1, Faction::GREEN));
  EXPECT_TRUE (dyn.IsPassable (c1, Faction::BLUE));

  EXPECT_TRUE (dyn.IsPassable (c2, Faction::RED));
  EXPECT_TRUE (dyn.IsPassable (c2, Faction::GREEN));
  EXPECT_FALSE (dyn.IsPassable (c2, Faction::BLUE));

  EXPECT_TRUE (dyn.IsPassable (c3, Faction::RED));
  EXPECT_TRUE (dyn.IsPassable (c3, Faction::GREEN));
  EXPECT_TRUE (dyn.IsPassable (c3, Faction::BLUE));
}

TEST_F (DynObstaclesTests, BuildingsFromDb)
{
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);

  DynObstacles dyn(db);

  EXPECT_FALSE (dyn.IsPassable (HexCoord (0, 2), Faction::RED));
  EXPECT_FALSE (dyn.IsPassable (HexCoord (0, 2), Faction::GREEN));
  EXPECT_FALSE (dyn.IsPassable (HexCoord (0, 2), Faction::BLUE));

  EXPECT_TRUE (dyn.IsPassable (HexCoord (2, 0), Faction::RED));
  EXPECT_TRUE (dyn.IsPassable (HexCoord (2, 0), Faction::GREEN));
  EXPECT_TRUE (dyn.IsPassable (HexCoord (2, 0), Faction::BLUE));
}

TEST_F (DynObstaclesTests, Modifications)
{
  const HexCoord c(42, 0);
  DynObstacles dyn(db);

  EXPECT_TRUE (dyn.IsPassable (c, Faction::RED));

  dyn.AddVehicle (c, Faction::RED);
  EXPECT_FALSE (dyn.IsPassable (c, Faction::RED));
  EXPECT_TRUE (dyn.IsPassable (c, Faction::GREEN));

  dyn.RemoveVehicle (c, Faction::RED);
  EXPECT_TRUE (dyn.IsPassable (c, Faction::RED));
  EXPECT_TRUE (dyn.IsPassable (c, Faction::BLUE));

  auto b = buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  EXPECT_TRUE (dyn.IsPassable (HexCoord (1, 0), Faction::RED));
  dyn.AddBuilding (*b);
  EXPECT_FALSE (dyn.IsPassable (HexCoord (1, 0), Faction::RED));
}

TEST_F (DynObstaclesTests, IsFree)
{
  auto b = buildings.CreateNew ("huesli", "", Faction::ANCIENT);
  b->SetCentre (HexCoord (0, 0));

  DynObstacles dyn(db);
  dyn.AddBuilding (*b);
  dyn.AddVehicle (HexCoord (1, 0), Faction::RED);
  dyn.AddVehicle (HexCoord (2, 0), Faction::GREEN);
  dyn.AddVehicle (HexCoord (3, 0), Faction::BLUE);

  EXPECT_TRUE (dyn.IsFree (HexCoord (0, 1)));
  EXPECT_FALSE (dyn.IsFree (HexCoord (0, 0)));
  EXPECT_FALSE (dyn.IsFree (HexCoord (1, 0)));
  EXPECT_FALSE (dyn.IsFree (HexCoord (2, 0)));
  EXPECT_FALSE (dyn.IsFree (HexCoord (3, 0)));
}

} // anonymous namespace
} // namespace pxd
