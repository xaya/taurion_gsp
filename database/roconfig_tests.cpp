/*
    GSP for the Taurion blockchain game
    Copyright (C) 2026  Autonomous Worlds Ltd

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

#include "roconfig.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class RoConfigStorageTests : public DBTestWithSchema
{

protected:

  RoConfigStorage tbl;

  RoConfigStorageTests ()
    : tbl(db)
  {}

  /**
   * Returns a config to store, marked by the given character cost.
   */
  static proto::ConfigData
  Config (const int64_t cost)
  {
    proto::ConfigData res;
    res.mutable_params ()->set_character_cost (cost);
    return res;
  }

};

TEST_F (RoConfigStorageTests, EmptyByDefault)
{
  EXPECT_EQ (tbl.Get (), nullptr);
}

TEST_F (RoConfigStorageTests, SetAndGet)
{
  tbl.Set (Config (42));
  EXPECT_EQ (tbl.Get ()->params ().character_cost (), 42);

  /* Another Set replaces the stored config.  */
  tbl.Set (Config (100));
  EXPECT_EQ (tbl.Get ()->params ().character_cost (), 100);

  /* A fresh wrapper sees the same stored config.  */
  RoConfigStorage other(db);
  EXPECT_EQ (other.Get ()->params ().character_cost (), 100);
}

} // anonymous namespace
} // namespace pxd
