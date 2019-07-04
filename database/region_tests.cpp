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

#include "region.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class RegionTests : public DBTestWithSchema
{

protected:

  /** RegionsTable instance for tests.  */
  RegionsTable tbl;

  RegionTests ()
    : tbl(db)
  {}

};

TEST_F (RegionTests, DefaultData)
{
  auto r = tbl.GetById (42);
  EXPECT_EQ (r->GetId (), 42);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (RegionTests, Update)
{
  tbl.GetById (42)->MutableProto ().set_prospecting_character (100);

  auto r = tbl.GetById (42);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 100);

  r = tbl.GetById (100);
  EXPECT_EQ (r->GetId (), 100);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (RegionTests, IdZero)
{
  tbl.GetById (0)->MutableProto ().set_prospecting_character (100);

  auto r = tbl.GetById (0);
  EXPECT_EQ (r->GetId (), 0);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 100);
}

TEST_F (RegionTests, DefaultNotWritten)
{
  tbl.GetById (42);
  auto res = tbl.QueryNonTrivial ();
  EXPECT_FALSE (res.Step ());
}

using RegionsTableTests = RegionTests;

TEST_F (RegionsTableTests, QueryNonTrivial)
{
  tbl.GetById (1);
  tbl.GetById (3)->MutableProto ();
  tbl.GetById (0)->MutableProto ();
  tbl.GetById (2);

  auto res = tbl.QueryNonTrivial ();

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 0);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 3);

  ASSERT_FALSE (res.Step ());
}

} // anonymous namespace
} // namespace pxd
