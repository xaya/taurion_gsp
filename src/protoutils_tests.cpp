#include "protoutils.hpp"

#include "hexagonal/coord.hpp"
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

} // anonymous namespace
} // namespace pxd
