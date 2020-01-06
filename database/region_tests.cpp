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
    : tbl(db, 1'042)
  {}

};

TEST_F (RegionTests, DefaultData)
{
  auto r = tbl.GetById (42);
  EXPECT_EQ (r->GetId (), 42);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (RegionTests, UpdateWithProto)
{
  auto r = tbl.GetById (42);
  r->MutableProto ().mutable_prospection ()->set_resource ("foo");
  r->SetResourceLeft (100);
  r.reset ();

  r = tbl.GetById (42);
  EXPECT_EQ (r->GetProto ().prospection ().resource (), "foo");
  EXPECT_EQ (r->GetResourceLeft (), 100);

  r = tbl.GetById (100);
  EXPECT_EQ (r->GetId (), 100);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (RegionTests, UpdateOnlyFields)
{
  auto r = tbl.GetById (42);
  r->MutableProto ().mutable_prospection ()->set_resource ("foo");
  r->SetResourceLeft (100);
  r.reset ();

  tbl.GetById (42)->SetResourceLeft (80);

  r = tbl.GetById (42);
  EXPECT_EQ (r->GetProto ().prospection ().resource (), "foo");
  EXPECT_EQ (r->GetResourceLeft (), 80);
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

TEST_F (RegionTests, ReadOnlyTable)
{
  RegionsTable ro(db, RegionsTable::HEIGHT_READONLY);

  auto r = tbl.GetById (42);
  r->MutableProto ().mutable_prospection ()->set_resource ("foo");
  r->SetResourceLeft (10);
  r.reset ();
  EXPECT_EQ (ro.GetById (42)->GetResourceLeft (), 10);

  tbl.GetById (42)->SetResourceLeft (1);
  EXPECT_EQ (ro.GetById (42)->GetResourceLeft (), 1);

  EXPECT_DEATH (ro.GetById (42)->SetResourceLeft (5), "readonly");
  EXPECT_DEATH (ro.GetById (42)->MutableProto (), "readonly");
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

TEST_F (RegionsTableTests, QueryModifiedSince)
{
  auto t = std::make_unique<RegionsTable> (db, 10);
  t->GetById (1)->MutableProto ();

  t = std::make_unique<RegionsTable> (db, 15);
  t->GetById (2)->MutableProto ().mutable_prospection ()->set_resource ("foo");

  auto res = tbl.QueryModifiedSince (15);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 2);
  ASSERT_FALSE (res.Step ());

  t = std::make_unique<RegionsTable> (db, 20);
  t->GetById (1)->MutableProto ();

  res = tbl.QueryModifiedSince (20);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 1);
  ASSERT_FALSE (res.Step ());

  t = std::make_unique<RegionsTable> (db, 30);
  t->GetById (2)->SetResourceLeft (10);

  res = tbl.QueryModifiedSince (20);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 1);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 2);
  ASSERT_FALSE (res.Step ());

  res = tbl.QueryModifiedSince (31);
  ASSERT_FALSE (res.Step ());
}

} // anonymous namespace
} // namespace pxd
