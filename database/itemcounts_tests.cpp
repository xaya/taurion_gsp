/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#include "itemcounts.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class ItemCountsTests : public DBTestWithSchema
{

protected:

  ItemCounts cnt;

  ItemCountsTests ()
    : cnt(db)
  {
    auto stmt = db.Prepare (R"(
      INSERT INTO `item_counts`
        (`name`, `found`)
        VALUES (?1, ?2)
    )");

    stmt.Bind<std::string> (1, "gold prize");
    stmt.Bind (2, 10);
    stmt.Execute ();

    stmt.Reset ();
    stmt.Bind<std::string> (1, "bow bpo");
    stmt.Bind (2, 3);
    stmt.Execute ();
  }

};

TEST_F (ItemCountsTests, GetFound)
{
  EXPECT_EQ (cnt.GetFound ("gold prize"), 10);
  EXPECT_EQ (cnt.GetFound ("silver prize"), 0);
  EXPECT_EQ (cnt.GetFound ("bow bpo"), 3);
}

TEST_F (ItemCountsTests, IncrementFound)
{
  cnt.IncrementFound ("gold prize");
  EXPECT_EQ (cnt.GetFound ("gold prize"), 11);
  EXPECT_EQ (cnt.GetFound ("silver prize"), 0);

  cnt.IncrementFound ("silver prize");
  EXPECT_EQ (cnt.GetFound ("gold prize"), 11);
  EXPECT_EQ (cnt.GetFound ("silver prize"), 1);
}

} // anonymous namespace
} // namespace pxd
