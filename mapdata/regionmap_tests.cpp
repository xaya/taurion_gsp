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

#include "regionmap.hpp"

#include "dataio.hpp"
#include "tiledata.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/rangemap.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <cstdint>
#include <fstream>

namespace pxd
{
namespace
{

class RegionMapTests : public testing::Test
{

protected:

  RegionMap rm;

};

TEST_F (RegionMapTests, OutOfMap)
{
  EXPECT_NE (rm.GetRegionId (HexCoord (0, 4064)), RegionMap::OUT_OF_MAP);
  EXPECT_EQ (rm.GetRegionId (HexCoord (0, 4065)), RegionMap::OUT_OF_MAP);
}

TEST_F (RegionMapTests, MatchesOriginalData)
{
  std::ifstream in("regiondata.dat", std::ios_base::binary); 

  const size_t n = Read<int16_t> (in);
  const size_t m = Read<int16_t> (in);

  const size_t num = n * m;
  LOG (INFO)
      << "Checking region map for " << n << " * " << m
      << " = " << num << " tiles";

  for (size_t i = 0; i < num; ++i)
    {
      const auto x = Read<int16_t> (in);
      const auto y = Read<int16_t> (in);
      const HexCoord c(x, y);

      const auto id = Read<int32_t> (in);
      ASSERT_EQ (rm.GetRegionId (c), id) << "Mismatch for tile " << c;
    }
}

TEST_F (RegionMapTests, GetRegionShape)
{
  const HexCoord coords[] =
    {
      HexCoord (0, -4064),
      HexCoord (0, 4064),
      HexCoord (-4064, 0),
      HexCoord (4064, 0),
      HexCoord (0, 0),
    };

  for (const auto& c : coords)
    {
      RegionMap::IdT id;
      const std::set<HexCoord> tiles = rm.GetRegionShape (c, id);

      EXPECT_EQ (id, rm.GetRegionId (c));

      for (const auto& t : tiles)
        {
          EXPECT_EQ (id, rm.GetRegionId (t));
          for (const auto& n : t.Neighbours ())
            {
              if (tiles.count (n) > 0)
                continue;
              EXPECT_NE (id, rm.GetRegionId (n));
            }
        }
    }
}

/**
 * Tests GetRegionShape exhaustively, which means that the method is invoked
 * for each region on the full map and we verify that it works as well as
 * that this yields a full (and disjoint) covering of all map tiles.
 *
 * This test is expensive, so that it is marked as disabled by default.  It
 * should still run fine, though.
 */
TEST_F (RegionMapTests, DISABLED_ExhaustiveRegionShapes)
{
  FullRangeMap<bool> tilesFound(false);
  unsigned numFound = 0;
  unsigned numTiles = 0;

  for (int y = tiledata::minY; y <= tiledata::maxY; ++y)
    {
      const int yInd = y - tiledata::minY;
      for (int x = tiledata::minX[yInd]; x <= tiledata::maxX[yInd]; ++x)
        {
          ++numTiles;

          const HexCoord c(x, y);
          if (tilesFound.Get (c))
            continue;

          RegionMap::IdT id;
          const std::set<HexCoord> tiles = rm.GetRegionShape (c, id);

          for (const auto& c : tiles)
            {
              ASSERT_EQ (rm.GetRegionId (c), id);

              auto found = tilesFound.Access  (c);
              ASSERT_FALSE (found);
              found = true;
              ++numFound;
            }
        }
    }

  EXPECT_EQ (numFound, numTiles);
}

} // anonymous namespace
} // namespace pxd
