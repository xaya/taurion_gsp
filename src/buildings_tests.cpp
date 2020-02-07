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

#include "database/dbtest.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <glog/logging.h>

namespace pxd
{
namespace
{

using testing::UnorderedElementsAre;

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

} // anonymous namespace
} // namespace pxd
