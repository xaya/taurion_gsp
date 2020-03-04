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

#include "account.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class AccountTests : public DBTestWithSchema
{

protected:

  AccountsTable tbl;

  AccountTests ()
    : tbl(db)
  {}

};

TEST_F (AccountTests, DefaultData)
{
  auto a = tbl.CreateNew ("foobar", Faction::BLUE);
  EXPECT_EQ (a->GetName (), "foobar");
  EXPECT_EQ (a->GetFaction (), Faction::BLUE);
  EXPECT_EQ (a->GetProto ().kills (), 0);
  EXPECT_EQ (a->GetProto ().fame (), 100);
  EXPECT_EQ (a->GetBalance (), 0);
}

TEST_F (AccountTests, Update)
{
  auto a = tbl.CreateNew ("foobar", Faction::RED);
  a->MutableProto ().set_kills (50);
  a->MutableProto ().set_fame (200);
  a.reset ();

  a = tbl.GetByName ("foobar");
  EXPECT_EQ (a->GetName (), "foobar");
  EXPECT_EQ (a->GetProto ().kills (), 50);
  EXPECT_EQ (a->GetProto ().fame (), 200);
}

TEST_F (AccountTests, Balance)
{
  auto a = tbl.CreateNew ("foobar", Faction::RED);
  a->AddBalance (10);
  a->AddBalance (20);
  a.reset ();

  a = tbl.GetByName ("foobar");
  EXPECT_EQ (a->GetBalance (), 30);
  a->AddBalance (-30);
  EXPECT_EQ (a->GetBalance (), 0);
  a.reset ();
}

using AccountsTableTests = AccountTests;

TEST_F (AccountsTableTests, CreateAlreadyExisting)
{
  tbl.CreateNew ("domob", Faction::BLUE);
  EXPECT_DEATH (tbl.CreateNew ("domob", Faction::BLUE), "exists already");
}

TEST_F (AccountsTableTests, QueryInitialised)
{
  tbl.CreateNew ("foo", Faction::RED);
  tbl.CreateNew ("bar", Faction::GREEN)->MutableProto ().set_fame (10);

  auto res = tbl.QueryInitialised ();

  ASSERT_TRUE (res.Step ());
  auto a = tbl.GetFromResult (res);
  EXPECT_EQ (a->GetName (), "bar");
  EXPECT_EQ (a->GetFaction (), Faction::GREEN);
  EXPECT_EQ (a->GetProto ().fame (), 10);

  ASSERT_TRUE (res.Step ());
  a = tbl.GetFromResult (res);
  EXPECT_EQ (a->GetName (), "foo");
  EXPECT_EQ (a->GetFaction (), Faction::RED);
  EXPECT_EQ (a->GetProto ().fame (), 100);

  ASSERT_FALSE (res.Step ());
}

TEST_F (AccountsTableTests, GetByName)
{
  tbl.CreateNew ("foo", Faction::RED);

  auto h = tbl.GetByName ("foo");
  ASSERT_TRUE (h != nullptr);
  EXPECT_EQ (h->GetName (), "foo");
  EXPECT_EQ (h->GetFaction (), Faction::RED);

  EXPECT_TRUE (tbl.GetByName ("bar") == nullptr);
}

} // anonymous namespace
} // namespace pxd
