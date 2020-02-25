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

#include "fighter.hpp"

#include "building.hpp"
#include "character.hpp"
#include "dbtest.hpp"
#include "faction.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class FighterTests : public DBTestWithSchema
{

protected:

  BuildingsTable buildings;
  CharacterTable characters;

  FighterTable tbl;

  FighterTests ()
    : buildings(db), characters(db), tbl(buildings, characters)
  {}

};

TEST_F (FighterTests, Characters)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (2, 5));
  c->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (5);
  c->MutableHP ().set_armour (10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->MutableTarget ().set_id (42);
  c->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (10);
  c->MutableRegenData ().set_shield_regeneration_mhp (2);
  c.reset ();

  proto::TargetId targetId;
  targetId.set_type (proto::TargetId::TYPE_CHARACTER);

  /* Read and modify first character through Fighter interface.  */
  targetId.set_id (id1);
  auto f = tbl.GetForTarget (targetId);

  EXPECT_EQ (f.GetFaction (), Faction::RED);
  EXPECT_EQ (f.GetPosition (), HexCoord (2, 5));
  EXPECT_EQ (f.GetCombatData ().attacks_size (), 1);
  EXPECT_EQ (f.GetAttackRange (), 5);

  const auto id = f.GetId ();
  EXPECT_EQ (id.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (id.id (), id1);

  auto& hp = f.MutableHP ();
  EXPECT_EQ (hp.armour (), 10);
  hp.set_armour (5);

  proto::TargetId t;
  t.set_id (5);
  f.SetTarget (t);
  f.reset ();

  /* Read and modify second character through Fighter interface.  */
  targetId.set_id (id2);
  f = tbl.GetForTarget (targetId);

  EXPECT_EQ (f.GetFaction (), Faction::GREEN);
  EXPECT_EQ (f.GetTarget ().id (), 42);
  EXPECT_EQ (f.GetAttackRange (), 10);
  EXPECT_EQ (f.GetRegenData ().shield_regeneration_mhp (), 2);
  f.ClearTarget ();
  f.reset ();

  /* Verify modifications through the CharacterTable.  */
  c = characters.GetById (id1);
  EXPECT_EQ (c->GetTarget ().id (), 5);
  EXPECT_EQ (c->GetHP ().armour (), 5);
  EXPECT_FALSE (characters.GetById (id2)->GetTarget ().has_id ());
}

TEST_F (FighterTests, Buildings)
{
  auto b = buildings.CreateNew ("checkmark", "domob", Faction::BLUE);
  const auto id = b->GetId ();
  b->SetCentre (HexCoord (10, -12));
  b->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (10);
  b->MutableHP ().set_armour (10);
  b->MutableRegenData ().set_shield_regeneration_mhp (2);
  b.reset ();

  proto::TargetId targetId;
  targetId.set_type (proto::TargetId::TYPE_BUILDING);
  targetId.set_id (id);

  auto f = tbl.GetForTarget (targetId);
  EXPECT_EQ (f.GetFaction (), Faction::BLUE);
  EXPECT_EQ (f.GetPosition (), HexCoord (10, -12));
  EXPECT_EQ (f.GetCombatData ().attacks_size (), 1);
  EXPECT_EQ (f.GetAttackRange (), 10);
  EXPECT_EQ (f.GetId ().type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (f.GetId ().id (), id);
  EXPECT_EQ (f.GetRegenData ().shield_regeneration_mhp (), 2);

  auto& hp = f.MutableHP ();
  EXPECT_EQ (hp.armour (), 10);
  hp.set_armour (5);

  proto::TargetId t;
  t.set_id (5);
  f.SetTarget (t);
  f.reset ();

  f = tbl.GetForTarget (targetId);
  EXPECT_EQ (f.GetTarget ().id (), 5);
  EXPECT_EQ (f.GetHP ().armour (), 5);
  f.ClearTarget ();
  f.reset ();

  b = buildings.GetById (id);
  EXPECT_FALSE (b->GetTarget ().has_id ());
}

TEST_F (FighterTests, GetForTarget)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idChar = c->GetId ();
  c->SetPosition (HexCoord (42, -35));
  c.reset ();

  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto idBuilding = b->GetId ();
  b->SetCentre (HexCoord (100, -100));
  b.reset ();

  proto::TargetId targetId;
  targetId.set_type (proto::TargetId::TYPE_CHARACTER);
  targetId.set_id (idChar);
  auto f = tbl.GetForTarget (targetId);
  EXPECT_EQ (f.GetPosition (), HexCoord (42, -35));
  f.MutableHP ().set_shield (42);
  f.reset ();

  targetId.set_type (proto::TargetId::TYPE_BUILDING);
  targetId.set_id (idBuilding);
  f = tbl.GetForTarget (targetId);
  EXPECT_EQ (f.GetPosition (), HexCoord (100, -100));
  f.MutableHP ().set_shield (80);
  f.reset ();

  targetId.set_type (proto::TargetId::TYPE_CHARACTER);
  targetId.set_id (idBuilding);
  EXPECT_TRUE (tbl.GetForTarget (targetId).empty ());

  targetId.set_type (proto::TargetId::TYPE_BUILDING);
  targetId.set_id (idChar);
  EXPECT_TRUE (tbl.GetForTarget (targetId).empty ());

  c = characters.GetById (idChar);
  EXPECT_EQ (c->GetHP ().shield (), 42);
  b = buildings.GetById (idBuilding);
  EXPECT_EQ (b->GetHP ().shield (), 80);
}

TEST_F (FighterTests, ProcessWithAttacks)
{
  buildings.CreateNew ("checkmark", "domob", Faction::GREEN);
  characters.CreateNew ("domob", Faction::GREEN);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idChar = c->GetId ();
  c->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (5);
  c.reset ();

  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto idBuilding = b->GetId ();
  b->MutableProto ().mutable_combat_data ()->add_attacks ()->set_range (5);
  b.reset ();

  unsigned cnt = 0;
  tbl.ProcessWithAttacks ([&] (Fighter f)
    {
      ++cnt;
      switch (cnt)
        {
        case 1:
          EXPECT_EQ (f.GetId ().type (), proto::TargetId::TYPE_BUILDING);
          EXPECT_EQ (f.GetId ().id (), idBuilding);
          break;

        case 2:
          EXPECT_EQ (f.GetId ().type (), proto::TargetId::TYPE_CHARACTER);
          EXPECT_EQ (f.GetId ().id (), idChar);
          break;

        default:
          FAIL () << "Too many regen-able fighters returned";
          break;
        }
    });
  EXPECT_EQ (cnt, 2);
}

TEST_F (FighterTests, ProcessForRegen)
{
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  buildings.CreateNew ("checkmark", "domob", Faction::GREEN);
  characters.CreateNew ("domob", Faction::GREEN);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idChar = c->GetId ();
  c->MutableHP ().set_shield (2);
  c->MutableRegenData ().mutable_max_hp ()->set_shield (10);
  c->MutableRegenData ().set_shield_regeneration_mhp (1);
  c.reset ();

  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto idBuilding = b->GetId ();
  b->MutableHP ().set_shield (2);
  b->MutableRegenData ().mutable_max_hp ()->set_shield (10);
  b->MutableRegenData ().set_shield_regeneration_mhp (1);
  b.reset ();

  unsigned cnt = 0;
  tbl.ProcessForRegen ([&] (Fighter f)
    {
      ++cnt;
      switch (cnt)
        {
        case 1:
          EXPECT_EQ (f.GetId ().type (), proto::TargetId::TYPE_BUILDING);
          EXPECT_EQ (f.GetId ().id (), idBuilding);
          break;

        case 2:
          EXPECT_EQ (f.GetId ().type (), proto::TargetId::TYPE_CHARACTER);
          EXPECT_EQ (f.GetId ().id (), idChar);
          break;

        default:
          FAIL () << "Too many regen-able fighters returned";
          break;
        }
    });
  EXPECT_EQ (cnt, 2);
}

TEST_F (FighterTests, ProcessWithTarget)
{
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  buildings.CreateNew ("checkmark", "domob", Faction::GREEN);
  characters.CreateNew ("domob", Faction::GREEN);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idChar = c->GetId ();
  c->MutableTarget ().set_id (5);
  c.reset ();

  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto idBuilding = b->GetId ();
  b->MutableTarget ().set_id (42);
  b.reset ();

  unsigned cnt = 0;
  tbl.ProcessWithTarget ([&] (Fighter f)
    {
      ++cnt;
      switch (cnt)
        {
        case 1:
          EXPECT_EQ (f.GetId ().type (), proto::TargetId::TYPE_BUILDING);
          EXPECT_EQ (f.GetId ().id (), idBuilding);
          break;

        case 2:
          EXPECT_EQ (f.GetId ().type (), proto::TargetId::TYPE_CHARACTER);
          EXPECT_EQ (f.GetId ().id (), idChar);
          break;

        default:
          FAIL () << "Too many targets returned";
          break;
        }
    });
  EXPECT_EQ (cnt, 2);
}

} // anonymous namespace
} // namespace pxd
