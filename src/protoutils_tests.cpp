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

#include "protoutils.hpp"

#include "hexagonal/coord.hpp"
#include "proto/character.pb.h"
#include "proto/geometry.pb.h"

#include <gtest/gtest.h>

#include <glog/logging.h>

namespace pxd
{
namespace
{

using ProtoCoordTests = testing::Test;

TEST_F (ProtoCoordTests, CoordToProto)
{
  const auto pb = CoordToProto (HexCoord (-3, 1));
  EXPECT_EQ (pb.x (), -3);
  EXPECT_EQ (pb.y (), 1);
}

TEST_F (ProtoCoordTests, CoordFromProto)
{
  proto::HexCoord pb;
  pb.set_x (42);
  pb.set_y (-2);

  EXPECT_EQ (CoordFromProto (pb), HexCoord (42, -2));
}

TEST_F (ProtoCoordTests, SetRepeatedCoords)
{
  proto::Movement mv;
  SetRepeatedCoords ({HexCoord (2, 3), HexCoord (-5, 5)},
                     *mv.mutable_waypoints ());

  ASSERT_EQ (mv.waypoints_size (), 2);
  EXPECT_EQ (CoordFromProto (mv.waypoints (0)), HexCoord (2, 3));
  EXPECT_EQ (CoordFromProto (mv.waypoints (1)), HexCoord (-5, 5));
}

TEST_F (ProtoCoordTests, SetRepeatedCoordsClears)
{
  proto::Movement mv;
  mv.mutable_waypoints ()->Add ()->set_x (5);

  SetRepeatedCoords ({}, *mv.mutable_waypoints ());
  EXPECT_EQ (mv.waypoints_size (), 0);
}

} // anonymous namespace
} // namespace pxd
