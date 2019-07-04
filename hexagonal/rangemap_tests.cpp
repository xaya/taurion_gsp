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

#include "rangemap.hpp"

#include "coord.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using RangeMapTests = testing::Test;

TEST_F (RangeMapTests, FullRangeAccess)
{
  const HexCoord centre(10, -5);
  const HexCoord::IntT range = 3;
  RangeMap<int> map(centre, range, -42);

  int counter = 0;
  for (int x = centre.GetX () - range; x <= centre.GetX () + range; ++x)
    for (int y = centre.GetY () - range; y <= centre.GetY () + range; ++y)
      {
        const HexCoord coord(x, y);
        if (HexCoord::DistanceL1 (coord, centre) > range)
          continue;

        EXPECT_EQ (map.Get (coord), -42);
        auto& entry = map.Access (coord);
        EXPECT_EQ (entry, -42);
        entry = ++counter;
        EXPECT_EQ (map.Get (coord), counter);
      }

  /* Verify the expected number of tiles in a 3-range.  */
  EXPECT_EQ (counter, 37);
}

TEST_F (RangeMapTests, ZeroRange)
{
  const HexCoord centre(10, -5);
  RangeMap<int> map(centre, 0, -42);

  EXPECT_EQ (map.Get (HexCoord (100, 100)), -42);

  auto& val = map.Access (centre);
  EXPECT_EQ (val, -42);
  val = 5;
  EXPECT_EQ (map.Get (centre), 5);
}

TEST_F (RangeMapTests, BoolValues)
{
  RangeMap<bool> map(HexCoord (0, 0), 10, false);

  EXPECT_FALSE (map.Get (HexCoord (2, 2)));

  auto val = map.Access (HexCoord (2, 2));
  EXPECT_FALSE (val);
  val = true;

  EXPECT_TRUE (map.Get (HexCoord (2, 2)));
}

TEST_F (RangeMapTests, OutOfRangeGet)
{
  RangeMap<int> map(HexCoord (0, 0), 10, -42);
  EXPECT_EQ (map.Get (HexCoord (100, 100)), -42);
}

TEST_F (RangeMapTests, OutOfRangeAccess)
{
  RangeMap<int> map(HexCoord (0, 0), 1, -42);
  EXPECT_EQ (map.Access (HexCoord (1, 0)), -42);
  EXPECT_DEATH (map.Access (HexCoord (2, 0)), "Out-of-range access");
}

} // anonymous namespace
} // namespace pxd
