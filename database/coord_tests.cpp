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

#include "coord.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace pxd
{
namespace
{

class CoordDatabaseTests : public DBTestFixture
{

protected:

  CoordDatabaseTests ()
  {
    auto stmt = db.Prepare (R"(
      CREATE TABLE `test` (
        `id` INTEGER PRIMARY KEY,
        `x` INTEGER NOT NULL,
        `y` INTEGER NOT NULL
      )
    )");
    stmt.Execute ();
  }

};

TEST_F (CoordDatabaseTests, RoundTrip)
{
  const std::vector<HexCoord> tests = {
    HexCoord (0, 0),
    HexCoord (-4000, 4000),
    HexCoord (12, 34),
  };

  for (const auto& t : tests)
    {
      const auto id = db.GetNextId ();
      auto stmt = db.Prepare (R"(
        INSERT INTO `test`
          (`id`, `x`, `y`)
          VALUES (?1, ?2, ?3)
      )");
      stmt.Bind (1, id);
      BindCoordParameter (stmt, 2, 3, t);
      stmt.Execute ();

      stmt = db.Prepare (R"(
        SELECT `x`, `y`
          FROM `test`
          WHERE `id` = ?1
      )");
      stmt.Bind (1, id);
      auto res = stmt.Query<ResultWithCoord> ();

      ASSERT_TRUE (res.Step ());
      EXPECT_EQ (GetCoordFromColumn (res), t);
      EXPECT_FALSE (res.Step ());
    }
}

} // anonymous namespace
} // namespace pxd
