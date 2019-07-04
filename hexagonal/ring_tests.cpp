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

#include "ring.hpp"

#include <gtest/gtest.h>

#include <array>
#include <set>

namespace pxd
{
namespace
{

using L1RingTests = testing::Test;

TEST_F (L1RingTests, RadiusZero)
{
  const L1Ring ring(HexCoord (5, 10), 0);
  auto it = ring.begin ();
  ASSERT_NE (it, ring.end ());
  EXPECT_EQ (*it, HexCoord (5, 10));
  ++it;
  EXPECT_EQ (it, ring.end ());
}

TEST_F (L1RingTests, Golden)
{
  const L1Ring ring(HexCoord (1, -1), 2);
  const std::array<HexCoord, 12> expected =
    {
      HexCoord (3, -1),
      HexCoord (3, -2), HexCoord (3, -3),
      HexCoord (2, -3), HexCoord (1, -3),
      HexCoord (0, -2), HexCoord (-1, -1),
      HexCoord (-1, 0), HexCoord (-1, 1),
      HexCoord (0, 1), HexCoord (1, 1),
      HexCoord (2, 0),
    };

  int i = 0;
  for (const HexCoord n : ring)
    {
      ASSERT_LT (i, expected.size ());
      EXPECT_EQ (n, expected[i]);
      ++i;
    }
  EXPECT_EQ (i, expected.size ());
}

TEST_F (L1RingTests, VariousRadii)
{
  const HexCoord centre(42, -100);
  for (HexCoord::IntT r = 0; r < 100; ++r)
    {
      const L1Ring ring(centre, r);

      std::set<HexCoord> coords;
      for (const HexCoord n : ring)
        {
          EXPECT_EQ (HexCoord::DistanceL1 (n, centre), r);

          const auto res = coords.insert (n);
          ASSERT_TRUE (res.second);
        }

      if (r == 0)
        EXPECT_EQ (coords.size (), 1);
      else
        EXPECT_EQ (coords.size (), 6 * r);
    }
}

} // anonymous namespace
} // namespace pxd
