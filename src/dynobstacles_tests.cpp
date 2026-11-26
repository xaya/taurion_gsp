/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2026  Autonomous Worlds Ltd

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

#include "testutils.hpp"

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

  ContextForTesting ctx;

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

  DynObstacles dyn(db, ctx);

  EXPECT_TRUE (dyn.HasVehicle (c1));
  EXPECT_TRUE (dyn.HasVehicle (c2));
  EXPECT_FALSE (dyn.HasVehicle (c3));
}

TEST_F (DynObstaclesTests, BuildingsFromDb)
{
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);

  DynObstacles dyn(db, ctx);

  EXPECT_TRUE (dyn.IsBuilding (HexCoord (0, 2)));
  EXPECT_FALSE (dyn.IsBuilding (HexCoord (2, 0)));
}

TEST_F (DynObstaclesTests, Modifications)
{
  const HexCoord c(42, 0);
  DynObstacles dyn(db, ctx);

  EXPECT_FALSE (dyn.HasVehicle (c));

  dyn.AddVehicle (c);
  EXPECT_TRUE (dyn.HasVehicle (c));

  dyn.RemoveVehicle (c);
  EXPECT_FALSE (dyn.HasVehicle (c));

  auto b = buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  EXPECT_FALSE (dyn.IsBuilding (HexCoord (1, 0)));
  dyn.AddBuilding (*b);
  EXPECT_TRUE (dyn.IsBuilding (HexCoord (1, 0)));
}

TEST_F (DynObstaclesTests, AddingRemovingBuildings)
{
  auto b1 = buildings.CreateNew ("checkmark", "", Faction::RED);
  auto b2 = buildings.CreateNew ("checkmark", "", Faction::GREEN);
  b2->SetCentre (HexCoord (10, 5));

  {
    DynObstacles dyn(db, ctx);
    dyn.AddBuilding (*b1);
    dyn.AddBuilding (*b2);
    EXPECT_DEATH (dyn.AddBuilding (*b1), "Error adding building");
  }

  {
    DynObstacles dyn(ctx.RoConfig ());
    std::vector<HexCoord> shape;
    ASSERT_TRUE (dyn.AddBuilding (b1->GetType (),
                                  b1->GetProto ().shape_trafo (),
                                  b1->GetCentre (), shape));
    ASSERT_TRUE (dyn.AddBuilding (b2->GetType (),
                                  b2->GetProto ().shape_trafo (),
                                  b2->GetCentre (), shape));
    ASSERT_FALSE (dyn.AddBuilding (b1->GetType (),
                                   b1->GetProto ().shape_trafo (),
                                   b1->GetCentre (), shape));
    EXPECT_TRUE (dyn.IsBuilding (HexCoord (0, 0)));
    dyn.RemoveBuilding (*b1);
    EXPECT_FALSE (dyn.IsBuilding (HexCoord (0, 0)));
    ASSERT_TRUE (dyn.AddBuilding (b1->GetType (),
                                  b1->GetProto ().shape_trafo (),
                                  b1->GetCentre (), shape));
  }
}

TEST_F (DynObstaclesTests, MultipleVehicles)
{
  const HexCoord c(10, 0);
  DynObstacles dyn(db, ctx);

  dyn.AddVehicle (c);
  dyn.AddVehicle (c);
  EXPECT_TRUE (dyn.HasVehicle (c));

  dyn.RemoveVehicle (c);
  EXPECT_TRUE (dyn.HasVehicle (c));

  dyn.RemoveVehicle (c);
  EXPECT_FALSE (dyn.HasVehicle (c));
}

TEST_F (DynObstaclesTests, IsFree)
{
  auto b = buildings.CreateNew ("huesli", "", Faction::ANCIENT);
  b->SetCentre (HexCoord (0, 0));

  DynObstacles dyn(db, ctx);
  dyn.AddBuilding (*b);
  dyn.AddVehicle (HexCoord (1, 0));

  EXPECT_TRUE (dyn.IsFree (HexCoord (0, 1)));
  EXPECT_FALSE (dyn.IsFree (HexCoord (0, 0)));
  EXPECT_FALSE (dyn.IsFree (HexCoord (1, 0)));
}

TEST_F (DynObstaclesTests, Gateways)
{
  auto b = buildings.CreateNew ("gated", "", Faction::ANCIENT);
  b->SetCentre (HexCoord (0, 0));
  auto other = buildings.CreateNew ("huesli", "", Faction::ANCIENT);
  other->SetCentre (HexCoord (0, 10));

  DynObstacles dyn(db, ctx);
  dyn.AddBuilding (*b);
  dyn.AddBuilding (*other);

  for (const auto& c : {HexCoord (5, 0), HexCoord (4, 0), HexCoord (5, 1)})
    EXPECT_TRUE (dyn.IsGateway (c)) << c;
  for (const auto& c : {HexCoord (0, 0), HexCoord (7, 0), HexCoord (0, 9)})
    EXPECT_FALSE (dyn.IsGateway (c)) << c;
  EXPECT_TRUE (dyn.IsFree (HexCoord (5, 0)));

  dyn.RemoveBuilding (*b);
  EXPECT_FALSE (dyn.IsGateway (HexCoord (5, 0)));
}

TEST_F (DynObstaclesTests, GatewayOffMap)
{
  /* With a changed config, the gateway of a building can reach off the map.
     Its tiles on the map are still kept.  */
  HexCoord edge(0, 0);
  while (ctx.Map ().IsOnMap (edge + HexCoord (1, 0)))
    edge += HexCoord (1, 0);

  auto b = buildings.CreateNew ("gated", "", Faction::ANCIENT);
  b->SetCentre (edge + HexCoord (-5, 0));

  DynObstacles dyn(db, ctx);
  dyn.AddBuilding (*b);
  EXPECT_TRUE (dyn.IsGateway (edge));
  EXPECT_TRUE (dyn.IsGateway (edge + HexCoord (-1, 0)));

  dyn.RemoveBuilding (*b);
  EXPECT_FALSE (dyn.IsGateway (edge));
}

} // anonymous namespace
} // namespace pxd
