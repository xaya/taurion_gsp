/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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
  auto a = tbl.CreateNew ("foobar");
  EXPECT_EQ (a->GetName (), "foobar");
  EXPECT_FALSE (a->IsInitialised ());
  EXPECT_EQ (a->GetFaction (), Faction::INVALID);
  EXPECT_EQ (a->GetProto ().kills (), 0);
  EXPECT_EQ (a->GetProto ().fame (), 100);
  EXPECT_EQ (a->GetBalance (), 0);
}

TEST_F (AccountTests, UpdateFields)
{
  auto a = tbl.CreateNew ("foobar");
  a->SetFaction (Faction::GREEN);
  a.reset ();

  a = tbl.GetByName ("foobar");
  EXPECT_TRUE (a->IsInitialised ());
  EXPECT_EQ (a->GetFaction (), Faction::GREEN);
}

TEST_F (AccountTests, UpdateProto)
{
  auto a = tbl.CreateNew ("foobar");
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
  auto a = tbl.CreateNew ("foobar");
  a->AddBalance (10);
  a->AddBalance (20);
  a.reset ();

  a = tbl.GetByName ("foobar");
  EXPECT_EQ (a->GetBalance (), 30);
  a->AddBalance (-30);
  EXPECT_EQ (a->GetBalance (), 0);
  a.reset ();
}

TEST_F (AccountTests, Skills)
{
  auto a = tbl.CreateNew ("foobar");
  EXPECT_EQ (a->Skills ()[proto::SKILL_BUILDING].GetXp (), 0);
  a->Skills ()[proto::SKILL_BUILDING].AddXp (10);
  EXPECT_EQ (a->Skills ()[proto::SKILL_BUILDING].GetXp (), 10);
  EXPECT_EQ (a->Skills ()[proto::SKILL_COMBAT].GetXp (), 0);
  a.reset ();

  a = tbl.CreateNew ("baz");
  EXPECT_EQ (a->Skills ()[proto::SKILL_BUILDING].GetXp (), 0);
  a->Skills ()[proto::SKILL_COMBAT].AddXp (10);
  a->Skills ()[proto::SKILL_COMBAT].AddXp (10);
  EXPECT_EQ (a->Skills ()[proto::SKILL_BUILDING].GetXp (), 0);
  EXPECT_EQ (a->Skills ()[proto::SKILL_COMBAT].GetXp (), 20);
  a.reset ();

  a = tbl.GetByName ("foobar");
  EXPECT_EQ (a->Skills ()[proto::SKILL_BUILDING].GetXp (), 10);
  a->Skills ()[proto::SKILL_BUILDING].AddXp (5);
  EXPECT_EQ (a->Skills ()[proto::SKILL_BUILDING].GetXp (), 15);
  a.reset ();

  a = tbl.GetByName ("foobar");
  EXPECT_EQ (a->Skills ()[proto::SKILL_BUILDING].GetXp (), 15);
  a.reset ();
}

using AccountsTableTests = AccountTests;

TEST_F (AccountsTableTests, CreateAlreadyExisting)
{
  tbl.CreateNew ("domob");
  EXPECT_DEATH (tbl.CreateNew ("domob"), "exists already");
}

TEST_F (AccountsTableTests, QueryAll)
{
  tbl.CreateNew ("uninit");
  tbl.CreateNew ("foo")->SetFaction (Faction::RED);

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto a = tbl.GetFromResult (res);
  EXPECT_EQ (a->GetName (), "foo");
  EXPECT_EQ (a->GetFaction (), Faction::RED);

  ASSERT_TRUE (res.Step ());
  a = tbl.GetFromResult (res);
  EXPECT_EQ (a->GetName (), "uninit");
  EXPECT_EQ (a->GetFaction (), Faction::INVALID);

  ASSERT_FALSE (res.Step ());
}

TEST_F (AccountsTableTests, QueryInitialised)
{
  tbl.CreateNew ("foo")->SetFaction (Faction::RED);
  tbl.CreateNew ("uninit");
  auto a = tbl.CreateNew ("bar");
  a->MutableProto ().set_fame (10);
  a->SetFaction (Faction::GREEN);
  a.reset ();

  auto res = tbl.QueryInitialised ();

  ASSERT_TRUE (res.Step ());
  a = tbl.GetFromResult (res);
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
  tbl.CreateNew ("foo");

  auto h = tbl.GetByName ("foo");
  ASSERT_TRUE (h != nullptr);
  EXPECT_EQ (h->GetName (), "foo");
  EXPECT_FALSE (h->IsInitialised ());

  EXPECT_TRUE (tbl.GetByName ("bar") == nullptr);
}

} // anonymous namespace
} // namespace pxd
