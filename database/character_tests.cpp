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

#include "character.hpp"

#include "dbtest.hpp"

#include "proto/character.pb.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using testing::ElementsAre;

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
  const auto id1 = c->GetId ();
  c->SetPosition (pos);
  c->SetEnterBuilding (10);
  c->MutableHP ().set_armour (10);
  c->MutableRegenData ().mutable_regeneration_mhp ()->set_shield (1'234);
  c.reset ();

  c = tbl.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetBuildingId (100);
  c->MutableProto ().mutable_movement ();
  c.reset ();

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  ASSERT_EQ (c->GetId (), id1);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::RED);
  ASSERT_FALSE (c->IsInBuilding ());
  EXPECT_EQ (c->GetPosition (), pos);
  EXPECT_EQ (c->GetEnterBuilding (), 10);
  EXPECT_EQ (c->GetHP ().armour (), 10);
  EXPECT_EQ (c->GetRegenData ().regeneration_mhp ().shield (), 1'234);
  EXPECT_FALSE (c->GetProto ().has_movement ());

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  ASSERT_EQ (c->GetId (), id2);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::GREEN);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (c->GetBuildingId (), 100);
  EXPECT_EQ (c->GetEnterBuilding (), Database::EMPTY_ID);
  EXPECT_FALSE (c->GetRegenData ().has_regeneration_mhp ());
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
  EXPECT_FALSE (c->IsBusy ());
  ASSERT_FALSE (res.Step ());

  c->SetOwner ("andy");
  c->SetPosition (pos);
  c->MutableVolatileMv ().set_partial_step (10);
  c->MutableHP ().set_shield (5);
  c->MutableRegenData ().mutable_regeneration_mhp ()->set_shield (1'234);
  c->MutableProto ().set_ongoing (105);
  c.reset ();

  res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::RED);
  EXPECT_EQ (c->GetPosition (), pos);
  EXPECT_EQ (c->GetVolatileMv ().partial_step (), 10);
  EXPECT_EQ (c->GetHP ().shield (), 5);
  EXPECT_EQ (c->GetRegenData ().regeneration_mhp ().shield (), 1'234);
  EXPECT_TRUE (c->IsBusy ());
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTests, ModificationFieldsOnly)
{
  const HexCoord pos(-2, 5);

  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetBuildingId (100);
  c.reset ();

  c = tbl.GetById (id);
  ASSERT_TRUE (c != nullptr);
  c->SetOwner ("andy");
  c->SetPosition (pos);
  c->SetEnterBuilding (42);
  c->MutableVolatileMv ().set_partial_step (24);
  c->MutableHP ().set_shield (5);
  c.reset ();

  c = tbl.GetById (id);
  ASSERT_TRUE (c != nullptr);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::RED);
  ASSERT_FALSE (c->IsInBuilding ());
  EXPECT_EQ (c->GetPosition (), pos);
  EXPECT_EQ (c->GetEnterBuilding (), 42);
  EXPECT_EQ (c->GetVolatileMv ().partial_step (), 24);
  EXPECT_EQ (c->GetHP ().shield (), 5);

  c->SetBuildingId (101);
  c.reset ();

  c = tbl.GetById (id);
  ASSERT_TRUE (c != nullptr);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (c->GetBuildingId (), 101);
}

TEST_F (CharacterTests, Target)
{
  auto h = tbl.CreateNew ("domob", Faction::RED);
  const auto id = h->GetId ();
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_FALSE (h->HasTarget ());
  proto::TargetId t;
  t.set_id(42);
  h->SetTarget (t);
  h.reset ();

  h = tbl.GetById (id);
  ASSERT_TRUE (h->HasTarget ());
  EXPECT_EQ (h->GetTarget ().id (), 42);
  h->ClearTarget ();
  h.reset ();

  EXPECT_FALSE (tbl.GetById (id)->HasTarget ());
}

TEST_F (CharacterTests, FriendlyTargets)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c.reset ();

  c = tbl.GetById (id);
  EXPECT_FALSE (c->HasFriendlyTargets ());
  c->SetFriendlyTargets (true);
  c.reset ();

  c = tbl.GetById (id);
  EXPECT_TRUE (c->HasFriendlyTargets ());
  c->SetFriendlyTargets (false);
  c.reset ();

  EXPECT_FALSE (tbl.GetById (id)->HasFriendlyTargets ());
}

TEST_F (CharacterTests, Inventory)
{
  auto h = tbl.CreateNew ("domob", Faction::RED);
  const auto id = h->GetId ();
  h->MutableProto ().set_cargo_space (100);
  h->GetInventory ().SetFungibleCount ("foo", 10);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 10);
  h->GetInventory ().SetFungibleCount ("foo", 0);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_TRUE (h->GetInventory ().IsEmpty ());
  h.reset ();
}

TEST_F (CharacterTests, CombatEffects)
{
  auto h = tbl.CreateNew ("domob", Faction::RED);
  const auto id = h->GetId ();
  EXPECT_FALSE (h->GetEffects ().has_speed ());
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_FALSE (h->GetEffects ().has_speed ());
  h->MutableEffects ().mutable_speed ()->set_percent (42);
  EXPECT_EQ (h->GetEffects ().speed ().percent (), 42);
  h.reset ();

  EXPECT_EQ (tbl.GetById (id)->GetEffects ().speed ().percent (), 42);
}

TEST_F (CharacterTests, AttackRange)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c.reset ();

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetAttackRange (false), CombatEntity::NO_ATTACKS);
  EXPECT_EQ (c->GetAttackRange (true), CombatEntity::NO_ATTACKS);
  auto* att = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  att->set_range (0);
  att = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  att->set_range (1);
  att->set_friendlies (true);
  c.reset ();

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetAttackRange (false), 0);
  EXPECT_EQ (c->GetAttackRange (true), 1);
  c->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (5);
  c.reset ();

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetAttackRange (false), 5);
  EXPECT_EQ (c->GetAttackRange (true), 1);
  c->MutableProto ().clear_combat_data ();
  c.reset ();

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetAttackRange (false), CombatEntity::NO_ATTACKS);
  EXPECT_EQ (c->GetAttackRange (true), CombatEntity::NO_ATTACKS);
}

TEST_F (CharacterTests, UsedCargoSpace)
{
  const RoConfig cfg(xaya::Chain::REGTEST);

  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->MutableProto ().set_cargo_space (1'000);
  c->GetInventory ().SetFungibleCount ("foo", 10);
  c->GetInventory ().SetFungibleCount ("bar", 3);

  EXPECT_EQ (c->UsedCargoSpace (cfg), 100 + 60);
  EXPECT_EQ (c->FreeCargoSpace (cfg), 1'000 - 160);
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

TEST_F (CharacterTableTests, QueryForBuilding)
{
  tbl.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id3 = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id4 = tbl.CreateNew ("domob", Faction::RED)->GetId ();

  tbl.GetById (id4)->SetBuildingId (10);
  tbl.GetById (id2)->SetBuildingId (10);
  tbl.GetById (id3)->SetBuildingId (42);

  auto res = tbl.QueryForBuilding (10);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), id2);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), id4);
  ASSERT_FALSE (res.Step ());

  res = tbl.QueryForBuilding (12345);
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

TEST_F (CharacterTableTests, QueryMining)
{
  tbl.CreateNew ("domob", Faction::RED)
    ->MutableProto ().mutable_mining ()->mutable_rate ();
  tbl.CreateNew ("andy", Faction::RED)
    ->MutableProto ().mutable_mining ()->set_active (true);

  auto res = tbl.QueryMining ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "andy");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, QueryWithAttacks)
{
  tbl.CreateNew ("domob", Faction::RED);
  tbl.CreateNew ("andy", Faction::RED)
    ->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (0);
  auto c = tbl.CreateNew ("inbuilding", Faction::RED);
  c->SetBuildingId (100);
  c->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (1);
  c.reset ();
  c = tbl.CreateNew ("daniel", Faction::RED);
  auto* att = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  att->set_area (1);
  att->set_friendlies (true);
  c.reset ();

  auto res = tbl.QueryWithAttacks ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "andy");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "daniel");
  ASSERT_FALSE (res.Step ());
}

/**
 * Utility function that sets regeneration-related data on a character.
 */
void
SetRegenData (Character& c, const bool armour, const unsigned rate,
              const unsigned maxHp, const unsigned hp)
{
  if (armour)
    {
      c.MutableRegenData ().mutable_regeneration_mhp ()->set_armour (rate);
      c.MutableRegenData ().mutable_max_hp ()->set_armour (maxHp);
      c.MutableHP ().set_armour (hp);
    }
  else
    {
      c.MutableRegenData ().mutable_regeneration_mhp ()->set_shield (rate);
      c.MutableRegenData ().mutable_max_hp ()->set_shield (maxHp);
      c.MutableHP ().set_shield (hp);
    }
}

TEST_F (CharacterTableTests, QueryForRegen)
{
  /* Set up a couple of characters that won't have any regeneration needs.
     Either immediately on creation, or because we updated them later on
     in a way that removed the need.  */

  SetRegenData (*tbl.CreateNew ("no regen", Faction::RED), false, 0, 10, 5);

  auto c = tbl.CreateNew ("no regen", Faction::RED);
  Database::IdT id = c->GetId ();
  SetRegenData (*c, false, 100, 10, 5);
  c.reset ();
  tbl.GetById (id)->MutableHP ().set_shield (10);

  c = tbl.CreateNew ("no regen", Faction::RED);
  id = c->GetId ();
  SetRegenData (*c, false, 100, 10, 5);
  c.reset ();
  tbl.GetById (id)
      ->MutableRegenData ().mutable_regeneration_mhp ()->set_shield (0);

  c = tbl.CreateNew ("no regen", Faction::RED);
  id = c->GetId ();
  SetRegenData (*c, true, 100, 10, 10);
  c.reset ();

  c = tbl.CreateNew ("no regen", Faction::RED);
  id = c->GetId ();
  SetRegenData (*c, true, 0, 10, 5);
  c.reset ();

  /* Set up characters that need regeneration.  Again either immediately
     or from updates.  */

  SetRegenData (*tbl.CreateNew ("needs from start", Faction::RED),
                false, 100, 10, 5);

  c = tbl.CreateNew ("hp update", Faction::RED);
  id = c->GetId ();
  SetRegenData (*c, false, 100, 10, 10);
  c.reset ();
  tbl.GetById (id)->MutableHP ().set_shield (5);

  c = tbl.CreateNew ("rate update", Faction::RED);
  id = c->GetId ();
  SetRegenData (*c, false, 0, 10, 5);
  c.reset ();
  tbl.GetById (id)
      ->MutableRegenData ().mutable_regeneration_mhp ()->set_shield (100);

  SetRegenData (*tbl.CreateNew ("armour", Faction::RED),
                true, 100, 10, 5);

  /* Iterate over all characters and do unrelated updates.  This ensures
     that the carrying over of the old "canregen" field works.  */
  auto res = tbl.QueryAll ();
  while (res.Step ())
    tbl.GetFromResult (res)->MutableVolatileMv ();

  /* Verify that we get the expected regeneration characters.  */
  std::vector<std::string> regenOwners;
  res = tbl.QueryForRegen ();
  while (res.Step ())
    regenOwners.push_back (tbl.GetFromResult (res)->GetOwner ());
  EXPECT_THAT (regenOwners, ElementsAre (
    "needs from start",
    "hp update",
    "rate update",
    "armour"
  ));
}

TEST_F (CharacterTableTests, QueryWithTarget)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  proto::TargetId t;
  t.set_id (5);
  c->SetTarget (t);
  c.reset ();

  const auto id2 = tbl.CreateNew ("andy", Faction::GREEN)->GetId ();

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

  tbl.GetById (id1)->SetFriendlyTargets (true);
  tbl.GetById (id2)->ClearTarget ();

  res = tbl.QueryWithTarget ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "domob");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, QueryForEnterBuilding)
{
  tbl.CreateNew ("not entering", Faction::RED);
  tbl.CreateNew ("entering 1", Faction::GREEN)->SetEnterBuilding (10);
  tbl.CreateNew ("entering 2", Faction::GREEN)->SetEnterBuilding (1);

  auto res = tbl.QueryForEnterBuilding ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "entering 1");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "entering 2");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, ProcessAllPositions)
{
  tbl.CreateNew ("red", Faction::RED)->SetPosition (HexCoord (1, 5));
  tbl.CreateNew ("red", Faction::RED)->SetPosition (HexCoord (-1, -5));
  tbl.CreateNew ("blue", Faction::BLUE)->SetPosition (HexCoord (0, 0));
  tbl.CreateNew ("green", Faction::GREEN)->SetBuildingId (100);

  struct Entry
  {

    Database::IdT id;
    Faction fact;
    HexCoord pos;

    Entry () = default;
    Entry (const Entry&) = default;

    Entry (const Database::IdT i, const Faction f, const HexCoord& p)
      : id(i), fact(f), pos(p)
    {}

    bool
    operator== (const Entry& o) const
    {
      return id == o.id && fact == o.fact && pos == o.pos;
    }

  };

  std::vector<Entry> entries;
  tbl.ProcessAllPositions ([&entries] (const Database::IdT id,
                                       const HexCoord& pos, const Faction f)
    {
      entries.emplace_back (id, f, pos);
    });

  EXPECT_THAT (entries, ElementsAre (Entry (1, Faction::RED, HexCoord (1, 5)),
                                     Entry (2, Faction::RED, HexCoord (-1, -5)),
                                     Entry (3, Faction::BLUE, HexCoord (0, 0))));
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

TEST_F (CharacterTableTests, CountForOwner)
{
  tbl.CreateNew ("domob", Faction::RED);
  const auto id = tbl.CreateNew ("andy", Faction::RED)->GetId ();
  tbl.CreateNew ("domob", Faction::RED);

  EXPECT_EQ (tbl.CountForOwner ("domob"), 2);
  EXPECT_EQ (tbl.CountForOwner ("andy"), 1);
  EXPECT_EQ (tbl.CountForOwner ("foo"), 0);

  tbl.DeleteById (id);
  EXPECT_EQ (tbl.CountForOwner ("domob"), 2);
  EXPECT_EQ (tbl.CountForOwner ("andy"), 0);
}

TEST_F (CharacterTableTests, ClearAllEffects)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->MutableProto ().set_speed (123);
  c->MutableEffects ().mutable_speed ()->set_percent (10);
  c.reset ();

  tbl.ClearAllEffects ();

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetProto ().speed (), 123);
  EXPECT_FALSE (c->GetEffects ().has_speed ());
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
