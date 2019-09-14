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

#include "faction.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace pxd
{
namespace
{

using FactionStringTests = testing::Test;

TEST_F (FactionStringTests, RoundTrip)
{
  const std::vector<std::pair<Faction, std::string>> tests = {
    {Faction::RED, "r"},
    {Faction::GREEN, "g"},
    {Faction::BLUE, "b"},
  };

  for (const auto& t : tests)
    {
      EXPECT_EQ (FactionToString (t.first), t.second);
      EXPECT_EQ (FactionFromString (t.second), t.first);
    }
}

TEST_F (FactionStringTests, InvalidString)
{
  for (const auto& s : {"", "x", "invalid"})
    EXPECT_EQ (FactionFromString (s), Faction::INVALID);
}

class FactionDatabaseTests : public DBTestFixture
{

protected:

  FactionDatabaseTests ()
  {
    auto stmt = db.Prepare (R"(
      CREATE TABLE `test` (
        `name` TEXT PRIMARY KEY,
        `faction` INTEGER NOT NULL
      )
    )");
    stmt.Execute ();
  }

};

TEST_F (FactionDatabaseTests, RoundTrip)
{
  const std::vector<std::pair<Faction, std::string>> tests = {
    {Faction::RED, "red"},
    {Faction::GREEN, "green"},
    {Faction::BLUE, "blue"},
  };

  for (const auto& t : tests)
    {
      auto stmt = db.Prepare (R"(
        INSERT INTO `test` (`name`, `faction`) VALUES (?1, ?2)
      )");
      stmt.Bind (1, t.second);
      BindFactionParameter (stmt, 2, t.first);
      stmt.Execute ();

      stmt = db.Prepare ("SELECT `faction` FROM `test` WHERE `name` = ?1");
      stmt.Bind (1, t.second);
      auto res = stmt.Query<ResultWithFaction> ();

      ASSERT_TRUE (res.Step ());
      EXPECT_EQ (GetFactionFromColumn (res), t.first);
      EXPECT_FALSE (res.Step ());
    }
}

TEST_F (FactionDatabaseTests, Invalid)
{
  auto stmt = db.Prepare (R"(
    INSERT INTO `test` (`name`, `faction`) VALUES ("foo", 0)
  )");
  stmt.Execute ();

  stmt = db.Prepare ("SELECT `faction` FROM `test`");
  auto res = stmt.Query<ResultWithFaction> ();

  ASSERT_TRUE (res.Step ());
  EXPECT_DEATH (GetFactionFromColumn (res), "Invalid faction value");
}

} // anonymous namespace
} // namespace pxd
