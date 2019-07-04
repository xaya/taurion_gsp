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

#include "dyntiles.hpp"

#include "tiledata.hpp"

#include "hexagonal/coord.hpp"

#include <gtest/gtest.h>

#include <functional>

namespace pxd
{
namespace
{

/**
 * Performs the callback for all hex coordinates on the full map.
 */
void
ForEachTile (const std::function<void (const HexCoord& c)>& cb)
{
  using namespace tiledata;
  for (HexCoord::IntT y = minY; y <= maxY; ++y)
    {
      const auto yInd = y - minY;
      for (HexCoord::IntT x = minX[yInd]; x <= maxX[yInd]; ++x)
        cb (HexCoord (x, y));
    }
}

using DynTilesTests = testing::Test;

TEST_F (DynTilesTests, FullMap)
{
  DynTiles<bool> m(true);
  ForEachTile ([&m] (const HexCoord& c)
    {
      ASSERT_TRUE (m.Get (c));
      auto ref = m.Access (c);
      ASSERT_TRUE (ref);
      ref = false;
    });
  ForEachTile ([&m] (const HexCoord& c)
    {
      ASSERT_FALSE (m.Get (c));
      ASSERT_FALSE (m.Access (c));
    });
}

} // anonymous namespace
} // namespace pxd
