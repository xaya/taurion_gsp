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

#include "character.hpp"

#include "dbtest.hpp"

#include "proto/character.pb.h"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/**
 * Sets the busy field of a character to the given value.  Makes sure to
 * add a prospection operation (or remove it) as necessary to make the
 * state consistent.
 */
void
SetBusy (Character& c, const unsigned val)
{
  c.SetBusy (val);
  if (val == 0)
    c.MutableProto ().clear_prospection ();
  else
    c.MutableProto ().mutable_prospection ();
}

/* ************************************************************************** */

class CharacterTests : public DBTestWithSchema
{

protected:

  /** CharacterTable instance for tests.  */
  CharacterTable tbl;

  CharacterTests ()
    : tbl(db)
  {}

};

TEST_F (CharacterTests, Creation)
{
  const HexCoord pos(5, -2);

  auto c = tbl.CreateNew  ("domob", Faction::RED);
  c->SetPosition (pos);
  const auto id1 = c->GetId ();
  c->MutableHP ().set_armour (10);
  SetBusy (*c, 42);
  c.reset ();

  c = tbl.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->MutableProto ().mutable_movement ();
  c.reset ();

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  ASSERT_EQ (c->GetId (), id1);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::RED);
  EXPECT_EQ (c->GetPosition (), pos);
  EXPECT_EQ (c->GetHP ().armour (), 10);
  EXPECT_EQ (c->GetBusy (), 42);
  EXPECT_FALSE (c->GetProto ().has_movement ());

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  ASSERT_EQ (c->GetId (), id2);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::GREEN);
  EXPECT_EQ (c->GetBusy (), 0);
  EXPECT_TRUE (c->GetProto ().has_movement ());

  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTests, ModificationWithProto)
{
  const HexCoord pos(-2, 5);

  tbl.CreateNew ("domob", Faction::RED);

  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetPosition (), HexCoord (0, 0));
  EXPECT_FALSE (c->GetVolatileMv ().has_partial_step ());
  EXPECT_FALSE (c->GetHP ().has_shield ());
  EXPECT_EQ (c->GetBusy (), 0);
  EXPECT_FALSE (c->GetProto ().has_target ());
  ASSERT_FALSE (res.Step ());

  c->SetOwner ("andy");
  c->SetPosition (pos);
  c->MutableVolatileMv ().set_partial_step (10);
  c->MutableHP ().set_shield (5);
  SetBusy (*c, 42);
  c->MutableProto ().mutable_target ();
  c.reset ();

  res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::RED);
  EXPECT_EQ (c->GetPosition (), pos);
  EXPECT_EQ (c->GetVolatileMv ().partial_step (), 10);
  EXPECT_EQ (c->GetHP ().shield (), 5);
  EXPECT_EQ (c->GetBusy (), 42);
  EXPECT_TRUE (c->GetProto ().has_target ());
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTests, ModificationFieldsOnly)
{
  const HexCoord pos(-2, 5);

  /* When we set the busy value using SetBusy, the proto gets modified.
     Only once we have the prospection proto set, we can modify the busy
     value without touching the proto.  Thus set up a non-zero value here
     and later modify just the value.  */
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  SetBusy (*c, 100);
  c.reset ();

  c = tbl.GetById (id);
  ASSERT_TRUE (c != nullptr);
  c->SetOwner ("andy");
  c->SetPosition (pos);
  c->MutableVolatileMv ().set_partial_step (24);
  c->MutableHP ().set_shield (5);
  c->SetBusy (42);
  c.reset ();

  c = tbl.GetById (id);
  ASSERT_TRUE (c != nullptr);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::RED);
  EXPECT_EQ (c->GetPosition (), pos);
  EXPECT_EQ (c->GetVolatileMv ().partial_step (), 24);
  EXPECT_EQ (c->GetHP ().shield (), 5);
  EXPECT_EQ (c->GetBusy (), 42);
}

/* ************************************************************************** */

using CharacterTableTests = CharacterTests;

TEST_F (CharacterTableTests, GetById)
{
  const auto id1 = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = tbl.CreateNew ("andy", Faction::RED)->GetId ();

  CHECK (tbl.GetById (500) == nullptr);
  CHECK_EQ (tbl.GetById (id1)->GetOwner (), "domob");
  CHECK_EQ (tbl.GetById (id2)->GetOwner (), "andy");
}

TEST_F (CharacterTableTests, QueryForOwner)
{
  tbl.CreateNew ("domob", Faction::RED);
  tbl.CreateNew ("domob", Faction::GREEN);
  tbl.CreateNew ("andy", Faction::BLUE);

  auto res = tbl.QueryForOwner ("domob");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetFaction (), Faction::RED);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetFaction (), Faction::GREEN);
  ASSERT_FALSE (res.Step ());

  res = tbl.QueryForOwner ("not there");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, QueryMoving)
{
  tbl.CreateNew ("domob", Faction::RED);
  tbl.CreateNew ("andy", Faction::RED)
    ->MutableProto ().mutable_movement ();

  auto res = tbl.QueryMoving ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "andy");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, QueryWithTarget)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->MutableProto ().mutable_target ()->set_id (5);
  c.reset ();

  const auto id2 = tbl.CreateNew ("andy", Faction::GREEN)->GetId ();

  auto res = tbl.QueryWithTarget ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "domob");
  ASSERT_FALSE (res.Step ());

  tbl.GetById (id1)->MutableProto ().clear_target ();
  tbl.GetById (id2)->MutableProto ().mutable_target ()->set_id (42);

  res = tbl.QueryWithTarget ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "andy");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, QueryBusyDone)
{
  tbl.CreateNew ("leisurely", Faction::RED);
  SetBusy (*tbl.CreateNew ("verybusy", Faction::RED), 2);
  SetBusy (*tbl.CreateNew ("done 1", Faction::RED), 1);
  SetBusy (*tbl.CreateNew ("done 2", Faction::RED), 1);

  auto res = tbl.QueryBusyDone ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "done 1");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "done 2");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, DeleteById)
{
  const auto id1 = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = tbl.CreateNew ("domob", Faction::RED)->GetId ();

  EXPECT_TRUE (tbl.GetById (id1) != nullptr);
  EXPECT_TRUE (tbl.GetById (id2) != nullptr);
  tbl.DeleteById (id1);
  EXPECT_TRUE (tbl.GetById (id1) == nullptr);
  EXPECT_TRUE (tbl.GetById (id2) != nullptr);
  tbl.DeleteById (id2);
  EXPECT_TRUE (tbl.GetById (id1) == nullptr);
  EXPECT_TRUE (tbl.GetById (id2) == nullptr);
}

TEST_F (CharacterTableTests, DecrementBusy)
{
  const auto id1 = tbl.CreateNew ("leisurely", Faction::RED)->GetId ();

  auto c = tbl.CreateNew ("verybusy", Faction::RED);
  const auto id2 = c->GetId ();
  SetBusy (*c, 10);
  c.reset ();

  tbl.DecrementBusy ();
  EXPECT_EQ (tbl.GetById (id1)->GetBusy (), 0);
  EXPECT_EQ (tbl.GetById (id2)->GetBusy (), 9);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
