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

#include "building.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class BuildingTests : public DBTestWithSchema
{

protected:

  BuildingsTable tbl;

  BuildingTests ()
    : tbl(db)
  {}

};

TEST_F (BuildingTests, Creation)
{
  const HexCoord pos(5, -2);

  auto h = tbl.CreateNew  ("refinery", "", Faction::ANCIENT);
  h->SetCentre (pos);
  const auto id1 = h->GetId ();
  h.reset ();

  h = tbl.CreateNew ("turret", "andy", Faction::GREEN);
  const auto id2 = h->GetId ();
  h->MutableProto ().mutable_shape_trafo ()->set_rotation_steps (3);
  h.reset ();

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  ASSERT_EQ (h->GetId (), id1);
  EXPECT_EQ (h->GetType (), "refinery");
  EXPECT_EQ (h->GetFaction (), Faction::ANCIENT);
  EXPECT_EQ (h->GetCentre (), pos);
  EXPECT_FALSE (h->GetProto ().has_shape_trafo ());

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  ASSERT_EQ (h->GetId (), id2);
  EXPECT_EQ (h->GetType (), "turret");
  EXPECT_EQ (h->GetOwner (), "andy");
  EXPECT_EQ (h->GetFaction (), Faction::GREEN);
  EXPECT_EQ (h->GetCentre (), HexCoord (0, 0));
  EXPECT_EQ (h->GetProto ().shape_trafo ().rotation_steps (), 3);

  ASSERT_FALSE (res.Step ());
}

TEST_F (BuildingTests, ModificationOfProto)
{
  const HexCoord pos(5, -2);
  auto h = tbl.CreateNew ("refinery", "domob", Faction::RED);
  const auto id = h->GetId ();
  h->SetCentre (pos);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_FALSE (h->GetProto ().has_shape_trafo ());
  h->MutableProto ().mutable_shape_trafo ()->set_rotation_steps (4);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_EQ (h->GetType (), "refinery");
  EXPECT_EQ (h->GetOwner (), "domob");
  EXPECT_EQ (h->GetFaction (), Faction::RED);
  EXPECT_EQ (h->GetCentre (), pos);
  EXPECT_EQ (h->GetProto ().shape_trafo ().rotation_steps (), 4);
}

TEST_F (BuildingTests, ModificationOfFields)
{
  const auto id = tbl.CreateNew ("refinery", "domob", Faction::RED)->GetId ();

  auto h = tbl.GetById (id);
  EXPECT_EQ (h->GetOwner (), "domob");
  h->SetOwner ("andy");
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_EQ (h->GetType (), "refinery");
  EXPECT_EQ (h->GetOwner (), "andy");
  EXPECT_EQ (h->GetFaction (), Faction::RED);
}

TEST_F (BuildingTests, CombatFields)
{
  const auto id = tbl.CreateNew ("turret", "andy", Faction::RED)->GetId ();

  auto h = tbl.GetById (id);
  EXPECT_EQ (h->GetAttackRange (), CombatEntity::NO_ATTACKS);
  auto* att = h->MutableProto ().mutable_combat_data ()->add_attacks ();
  att->set_range (5);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_EQ (h->GetProto ().combat_data ().attacks_size (), 1);
  EXPECT_EQ (h->GetAttackRange (), 5);
  h->MutableHP ().set_armour (10);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_EQ (h->GetHP ().armour (), 10);
  h->MutableRegenData ().set_shield_regeneration_mhp (42);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_EQ (h->GetRegenData ().shield_regeneration_mhp (), 42);
  EXPECT_FALSE (h->HasTarget ());
  proto::TargetId t;
  t.set_id (50);
  h->SetTarget (t);
  h.reset ();

  h = tbl.GetById (id);
  ASSERT_TRUE (h->HasTarget ());
  EXPECT_EQ (h->GetTarget ().id (), 50);
  h->ClearTarget ();
  h.reset ();

  EXPECT_FALSE (tbl.GetById (id)->HasTarget ());
}

TEST_F (BuildingTests, DummyCombatEffects)
{
  auto h = tbl.CreateNew ("checkmark", "andy", Faction::RED);
  h->MutableEffects ().mutable_speed ()->set_percent (42);
  EXPECT_FALSE (h->GetEffects ().has_speed ());
}

/* ************************************************************************** */

using BuildingsTableTests = BuildingTests;

TEST_F (BuildingsTableTests, GetById)
{
  const auto id1 = tbl.CreateNew ("turret", "domob", Faction::RED)->GetId ();
  const auto id2 = tbl.CreateNew ("turret", "andy", Faction::RED)->GetId ();

  CHECK (tbl.GetById (500) == nullptr);
  CHECK_EQ (tbl.GetById (id1)->GetOwner (), "domob");
  CHECK_EQ (tbl.GetById (id2)->GetOwner (), "andy");
}

TEST_F (BuildingsTableTests, QueryAll)
{
  tbl.CreateNew ("turret", "domob", Faction::RED);
  tbl.CreateNew ("turret", "andy", Faction::RED);

  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "domob");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "andy");
  ASSERT_FALSE (res.Step ());
}

TEST_F (BuildingsTableTests, QueryWithAttacks)
{
  tbl.CreateNew ("checkmark", "domob", Faction::RED);
  tbl.CreateNew ("checkmark", "andy", Faction::RED)
    ->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (0);

  auto res = tbl.QueryWithAttacks ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "andy");
  ASSERT_FALSE (res.Step ());
}

TEST_F (BuildingsTableTests, QueryForRegen)
{
  /* This test is much more basic than the one for CharacterTable,
     because the underlying code is shared anyway.  We do not cover all
     the different cases here, just check that the function works
     in general (i.e. queries correctly in SQL).  */

  tbl.CreateNew ("checkmark", "no regen", Faction::RED);

  auto h = tbl.CreateNew ("checkmark", "can regen", Faction::RED);
  h->MutableHP ().set_shield (10);
  h->MutableRegenData ().mutable_max_hp ()->set_shield (100);
  h->MutableRegenData ().set_shield_regeneration_mhp (1);
  h.reset ();

  auto res = tbl.QueryForRegen ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "can regen");
  ASSERT_FALSE (res.Step ());
}

TEST_F (BuildingsTableTests, QueryWithTarget)
{
  auto h = tbl.CreateNew ("turret", "domob", Faction::RED);
  const auto id1 = h->GetId ();
  proto::TargetId t;
  t.set_id (5);
  h->SetTarget (t);
  h.reset ();

  const auto id2 = tbl.CreateNew ("turret", "andy", Faction::GREEN)->GetId ();

  auto res = tbl.QueryWithTarget ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "domob");
  ASSERT_FALSE (res.Step ());

  tbl.GetById (id1)->ClearTarget ();
  tbl.GetById (id2)->SetTarget (t);

  res = tbl.QueryWithTarget ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "andy");
  ASSERT_FALSE (res.Step ());
}

TEST_F (BuildingsTableTests, DeleteById)
{
  auto id1 = tbl.CreateNew ("turret", "domob", Faction::RED)->GetId ();
  auto id2 = tbl.CreateNew ("turret", "andy", Faction::GREEN)->GetId ();

  ASSERT_NE (tbl.GetById (id2), nullptr);
  tbl.DeleteById (id2);
  ASSERT_EQ (tbl.GetById (id2), nullptr);

  auto h = tbl.GetById (id1);
  ASSERT_NE (h, nullptr);
  EXPECT_EQ (h->GetOwner (), "domob");
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
