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
      auto res = stmt.Query ();

      ASSERT_TRUE (res.Step ());
      EXPECT_EQ (GetFactionFromColumn (res, "faction"), t.first);
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
  auto res = stmt.Query ();

  ASSERT_TRUE (res.Step ());
  EXPECT_DEATH (GetFactionFromColumn (res, "faction"), "Invalid faction value");
}

} // anonymous namespace
} // namespace pxd
