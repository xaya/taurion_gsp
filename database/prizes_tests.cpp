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

#include "prizes.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class PrizesTests : public DBTestWithSchema
{

protected:

  /** Prizes instance for tests.  */
  Prizes p;

  PrizesTests ()
    : p(db)
  {
    auto stmt = db.Prepare (R"(
      INSERT INTO `prizes` (`name`, `found`) VALUES (?1, ?2)
    )");

    stmt.Bind<std::string> (1, "gold");
    stmt.Bind (2, 10);
    stmt.Execute ();

    stmt.Reset ();
    stmt.Bind<std::string> (1, "silver");
    stmt.Bind (2, 0);
    stmt.Execute ();
  }

};

TEST_F (PrizesTests, GetFound)
{
  EXPECT_EQ (p.GetFound ("gold"), 10);
  EXPECT_EQ (p.GetFound ("silver"), 0);
}

TEST_F (PrizesTests, IncrementFound)
{
  p.IncrementFound ("gold");
  EXPECT_EQ (p.GetFound ("gold"), 11);
  EXPECT_EQ (p.GetFound ("silver"), 0);

  p.IncrementFound ("silver");
  EXPECT_EQ (p.GetFound ("gold"), 11);
  EXPECT_EQ (p.GetFound ("silver"), 1);
}

} // anonymous namespace
} // namespace pxd
