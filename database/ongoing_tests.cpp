/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "ongoing.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class OngoingOperationTests : public DBTestWithSchema
{

protected:

  OngoingsTable tbl;

  OngoingOperationTests ()
    : tbl(db)
  {}

  OngoingsTable::Handle
  Create ()
  {
    return tbl.CreateNew (12345);
  }

};

TEST_F (OngoingOperationTests, DefaultData)
{
  auto op = Create ();
  EXPECT_EQ (op->GetHeight (), 0);
  EXPECT_EQ (op->GetCharacterId (), Database::EMPTY_ID);
  EXPECT_EQ (op->GetBuildingId (), Database::EMPTY_ID);
  EXPECT_EQ (op->GetProto ().op_case (), proto::OngoingOperation::OP_NOT_SET);
}

TEST_F (OngoingOperationTests, Update)
{
  auto op = Create ();
  const auto id1 = op->GetId ();
  op->SetHeight (5);
  op->SetCharacterId (10);
  op->MutableProto ().mutable_prospection ();
  op.reset ();

  op = Create ();
  const auto id2 = op->GetId ();
  op->SetBuildingId (20);
  op->MutableProto ().mutable_armour_repair ();
  op.reset ();

  op = tbl.GetById (id1);
  EXPECT_EQ (op->GetHeight (), 5);
  EXPECT_EQ (op->GetCharacterId (), 10);
  EXPECT_EQ (op->GetBuildingId (), Database::EMPTY_ID);
  EXPECT_TRUE (op->GetProto ().has_prospection ());
  op->MutableProto ().clear_prospection ();
  op.reset ();

  op = tbl.GetById (id2);
  EXPECT_EQ (op->GetCharacterId (), Database::EMPTY_ID);
  EXPECT_EQ (op->GetBuildingId (), 20);
  EXPECT_TRUE (op->GetProto ().has_armour_repair ());
  op->SetCharacterId (30);
  op->SetBuildingId (Database::EMPTY_ID);
  op->MutableProto ().clear_prospection ();
  op.reset ();

  op = tbl.GetById (id1);
  EXPECT_EQ (op->GetProto ().op_case (), proto::OngoingOperation::OP_NOT_SET);
  op.reset ();

  op = tbl.GetById (id2);
  EXPECT_EQ (op->GetCharacterId (), 30);
  EXPECT_EQ (op->GetBuildingId (), Database::EMPTY_ID);
  op.reset ();
}

using OngoingsTableTests = OngoingOperationTests;

TEST_F (OngoingsTableTests, QueryAll)
{
  Create ()->SetHeight (10);
  Create ()->SetHeight (5);

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetHeight (), 10);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetHeight (), 5);

  ASSERT_FALSE (res.Step ());
}

TEST_F (OngoingsTableTests, QueryForHeight)
{
  auto op = Create ();
  op->SetHeight (5);
  op->SetCharacterId (2);
  op.reset ();

  op = Create ();
  op->SetHeight (6);
  op->SetCharacterId (5);
  op.reset ();

  op = Create ();
  op->SetHeight (5);
  op->SetCharacterId (1);
  op.reset ();

  auto res = tbl.QueryForHeight (5);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetCharacterId (), 2);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetCharacterId (), 1);

  ASSERT_FALSE (res.Step ());
}

TEST_F (OngoingsTableTests, QueryForBuilding)
{
  auto op = Create ();
  op->SetHeight (1);
  op->SetBuildingId (42);
  op.reset ();

  op = Create ();
  op->SetHeight (2);
  op->SetBuildingId (5);
  op.reset ();

  op = Create ();
  op->SetHeight (3);
  op->SetCharacterId (42);
  op.reset ();

  auto res = tbl.QueryForBuilding (42);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetHeight (), 1);

  ASSERT_FALSE (res.Step ());
}

TEST_F (OngoingsTableTests, DeleteForCharacter)
{
  db.SetNextId (101);

  Create ()->SetCharacterId (42);
  Create ()->SetBuildingId (42);
  Create ()->SetCharacterId (50);

  tbl.DeleteForCharacter (42);
  tbl.DeleteForCharacter (12345);

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 102);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 103);

  ASSERT_FALSE (res.Step ());
}

TEST_F (OngoingsTableTests, DeleteForBuilding)
{
  db.SetNextId (101);

  Create ()->SetBuildingId (42);
  Create ()->SetCharacterId (42);
  Create ()->SetBuildingId (50);

  tbl.DeleteForBuilding (42);
  tbl.DeleteForBuilding (12345);

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 102);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 103);

  ASSERT_FALSE (res.Step ());
}

TEST_F (OngoingsTableTests, DeleteForHeight)
{
  db.SetNextId (101);

  Create ()->SetHeight (50);
  Create ()->SetHeight (42);
  Create ()->SetHeight (10);

  tbl.DeleteForHeight (42);

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 101);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 103);

  ASSERT_FALSE (res.Step ());
}

} // anonymous namespace
} // namespace pxd
