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

#include "database.hpp"

#include "dbtest.hpp"

#include "proto/geometry.pb.h"

#include <gtest/gtest.h>

#include <vector>

namespace pxd
{
namespace
{

/**
 * Basic struct that holds data corresponding to what we store in the test
 * database table.
 */
struct RowData
{
  int64_t id;
  bool flag;
  std::string name;
  int coordX;
  int coordY;
};

/**
 * Database result type for our test table.
 */
struct TestResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (bool, flag, 2);
  RESULT_COLUMN (std::string, name, 3);
  RESULT_COLUMN (pxd::proto::HexCoord, proto, 4);
};

class DatabaseTests : public DBTestFixture
{

protected:

  DatabaseTests ()
  {
    auto stmt = db.Prepare (R"(
      CREATE TABLE `test` (
        `id` INTEGER PRIMARY KEY,
        `flag` INTEGER,
        `name` TEXT,
        `proto` BLOB
      )
    )");
    stmt.Execute ();
  }

  /**
   * Queries the data to verify that it matches the given golden values.
   */
  void
  ExpectData (const std::vector<RowData>& golden)
  {
    auto stmt = db.Prepare (R"(
      SELECT * FROM `test` ORDER BY `id` ASC
    )");
    auto res = stmt.Query<TestResult> ();
    for (const auto& val : golden)
      {
        LOG (INFO) << "Verifying golden data with ID " << val.id << "...";

        ASSERT_TRUE (res.Step ());
        EXPECT_EQ (res.Get<TestResult::id> (), val.id);
        EXPECT_EQ (res.Get<TestResult::flag> (), val.flag);
        EXPECT_EQ (res.Get<TestResult::name> (), val.name);

        LazyProto<proto::HexCoord> c = res.GetProto<TestResult::proto> ();
        EXPECT_EQ (c.Get ().x (), val.coordX);
        EXPECT_EQ (c.Get ().y (), val.coordY);
      }
    ASSERT_FALSE (res.Step ());
  }

};

TEST_F (DatabaseTests, BindingAndQuery)
{
  LazyProto<proto::HexCoord> coord1;
  coord1.SetToDefault ();
  coord1.Mutable ().set_x (5);
  coord1.Mutable ().set_y (-3);

  LazyProto<proto::HexCoord> coord2;
  coord2.SetToDefault ();
  coord2.Mutable ().set_x (-4);
  coord2.Mutable ().set_y (0);

  auto stmt = db.Prepare (R"(
    INSERT INTO `test`
      (`id`, `flag`, `name`, `proto`) VALUES
      (?1, ?2, ?3, ?4), (?5, ?6, ?7, ?8);
  )");

  const auto largeInt = std::numeric_limits<int64_t>::max ();
  stmt.Bind (1, largeInt);
  stmt.BindNull (2);
  stmt.Bind<std::string> (3, "foo");
  stmt.BindProto (4, coord1);

  stmt.Bind (5, 10);
  stmt.Bind (6, true);
  stmt.Bind<std::string> (7, "bar");
  stmt.BindProto (8, coord2);

  stmt.Execute ();

  ExpectData ({
    {10, true, "bar", coord2.Get ().x (), coord2.Get ().y ()},
    {largeInt, false, "foo", coord1.Get ().x (), coord1.Get ().y ()},
  });
}

TEST_F (DatabaseTests, StatementReset)
{
  auto stmt = db.Prepare ("INSERT INTO `test` (`id`, `flag`) VALUES (?1, ?2)");

  stmt.Bind (1, 42);
  stmt.Bind (2, true);
  stmt.Execute ();

  stmt.Reset ();
  stmt.Bind (1, 50);
  /* Do not bind parameter 2, so it is NULL.  This verifies that the parameter
     bindings are reset completely.  */
  stmt.Execute ();

  stmt = db.Prepare ("SELECT `id`, `flag` FROM `test` ORDER BY `id`");
  auto res = stmt.Query<TestResult> ();

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (res.Get<TestResult::id> (), 42);
  EXPECT_EQ (res.Get<TestResult::flag> (), true);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (res.Get<TestResult::id> (), 50);
  EXPECT_EQ (res.Get<TestResult::flag> (), false);

  ASSERT_FALSE (res.Step ());
}

TEST_F (DatabaseTests, ProtoIsOverwritten)
{
  LazyProto<proto::HexCoord> coord;
  coord.SetToDefault ();
  coord.Mutable ().set_x (5);
  /* Explicitly leave y unset.  */

  auto stmt = db.Prepare (R"(
    INSERT INTO `test` (`proto`) VALUES (?1);
  )");
  stmt.BindProto (1, coord);
  stmt.Execute ();

  stmt = db.Prepare ("SELECT `proto` FROM `test`");
  auto res = stmt.Query<TestResult> ();

  ASSERT_TRUE (res.Step ());

  /* Verify that the input proto is fully overwritten (i.e. cleared) and not
     just merged with the data we read.  */
  LazyProto<proto::HexCoord> protoRes;
  protoRes.SetToDefault ();
  protoRes.Mutable ().set_y (42);
  protoRes = res.GetProto<TestResult::proto> ();
  EXPECT_EQ (protoRes.Get ().x (), coord.Get ().x ());
  EXPECT_FALSE (protoRes.Get ().has_y ());

  ASSERT_FALSE (res.Step ());
}

TEST_F (DatabaseTests, ResultProperties)
{
  auto stmt = db.Prepare ("SELECT * FROM `test`");
  auto res = stmt.Query<TestResult> ();
  EXPECT_EQ (&res.GetDatabase (), &db);
}

} // anonymous namespace
} // namespace pxd
