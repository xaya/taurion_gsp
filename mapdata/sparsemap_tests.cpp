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

#include "sparsemap.hpp"

#include <gtest/gtest.h>

namespace pxd
{

class SparseMapTests : public testing::Test
{

protected:

  static constexpr HexCoord COORD[] = {HexCoord (10, -20), HexCoord (-42, 0)};

  SparseTileMap<int> map;

  SparseMapTests ()
    : map(0)
  {}

  /**
   * Returns the size of the non-sparse entry map.
   */
  size_t
  GetNumEntries () const
  {
    return map.values.size ();
  }

};

constexpr HexCoord SparseMapTests::COORD[];

namespace
{

TEST_F (SparseMapTests, BasicAccess)
{
  EXPECT_EQ (map.Get (COORD[0]), 0);
  map.Set (COORD[0], 42);
  EXPECT_EQ (map.Get (COORD[0]), 42);
  EXPECT_EQ (map.Get (COORD[1]), 0);
  map.Set (COORD[1], 10);
  EXPECT_EQ (map.Get (COORD[0]), 42);
  EXPECT_EQ (map.Get (COORD[1]), 10);
  map.Set (COORD[0], 0);
  EXPECT_EQ (map.Get (COORD[0]), 0);
  EXPECT_EQ (map.Get (COORD[1]), 10);
}

TEST_F (SparseMapTests, EntriesClearedAgain)
{
  map.Set (COORD[0], 42);
  EXPECT_EQ (map.Get (COORD[0]), 42);
  EXPECT_EQ (GetNumEntries (), 1);
  map.Set (COORD[0], 0);
  EXPECT_EQ (map.Get (COORD[0]), 0);
  EXPECT_EQ (GetNumEntries (), 0);
}

} // anonymous namespace
} // namespace pxd
