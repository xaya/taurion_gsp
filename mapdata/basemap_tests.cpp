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

#include "basemap.hpp"

#include "dataio.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <cstdint>
#include <fstream>

namespace pxd
{
namespace
{

class BaseMapTests : public testing::Test
{

protected:

  BaseMap map;

  BaseMapTests ()
    : map(xaya::Chain::REGTEST)
  {}

};

TEST_F (BaseMapTests, IsOnMap)
{
  // Y boundary tests (X=0 is valid at all Y values within range)
  EXPECT_TRUE (map.IsOnMap (HexCoord (0, -4096)));   // minY
  EXPECT_TRUE (map.IsOnMap (HexCoord (0, 4095)));    // maxY
  EXPECT_FALSE (map.IsOnMap (HexCoord (0, -4097)));  // below minY
  EXPECT_FALSE (map.IsOnMap (HexCoord (0, 4096)));   // above maxY

  // X boundary tests at Y=0 (at center row, X ranges -4096 to 4095)
  EXPECT_TRUE (map.IsOnMap (HexCoord (-4096, 0)));
  EXPECT_TRUE (map.IsOnMap (HexCoord (4095, 0)));
  EXPECT_FALSE (map.IsOnMap (HexCoord (-4097, 0)));
  EXPECT_FALSE (map.IsOnMap (HexCoord (4096, 0)));
}

TEST_F (BaseMapTests, MatchesOriginalObstacleData)
{
  std::ifstream in("obstacledata.dat", std::ios_base::binary); 

  const size_t n = Read<int16_t> (in);
  const size_t m = Read<int16_t> (in);
  LOG (INFO)
      << "Checking IsPassable for " << n << " * " << m
      << " = " << (n * m) << " tiles";

  for (size_t i = 0; i < n * m; ++i)
    {
      const auto x = Read<int16_t> (in);
      const auto y = Read<int16_t> (in);
      const bool passable = Read<int16_t> (in);

      const HexCoord c(x, y);
      EXPECT_TRUE (map.IsOnMap (c));
      EXPECT_EQ (map.IsPassable (c), passable);
    }
}

TEST_F (BaseMapTests, EdgeWeights)
{
  const HexCoord a(0, 0);
  const HexCoord b(1, 0);
  EXPECT_EQ (map.GetEdgeWeight (a, b), 1000);

  // Test edge weight at X boundary (Y=0 row: X ranges -4096 to 4095)
  const HexCoord outside(-4097, 0);
  const HexCoord inside(-4096, 0);
  ASSERT_FALSE (map.IsOnMap (outside));
  ASSERT_TRUE (map.IsOnMap (inside));
  EXPECT_EQ (map.GetEdgeWeight (inside, outside), PathFinder::NO_CONNECTION);
}

} // anonymous namespace
} // namespace pxd
