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

class FighterTableTests : public DBTestWithSchema
{

protected:

  BuildingsTable buildings;
  CharacterTable characters;

  FighterTable tbl;

  FighterTableTests ()
    : buildings(db), characters(db), tbl(buildings, characters)
  {}

};

TEST_F (FighterTableTests, GetForTarget)
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
  EXPECT_EQ (tbl.GetForTarget (targetId)->GetCombatPosition (),
             HexCoord (42, -35));

  targetId.set_type (proto::TargetId::TYPE_BUILDING);
  targetId.set_id (idBuilding);
  EXPECT_EQ (tbl.GetForTarget (targetId)->GetCombatPosition (),
             HexCoord (100, -100));

  targetId.set_type (proto::TargetId::TYPE_CHARACTER);
  targetId.set_id (idBuilding);
  EXPECT_EQ (tbl.GetForTarget (targetId), nullptr);

  targetId.set_type (proto::TargetId::TYPE_BUILDING);
  targetId.set_id (idChar);
  EXPECT_EQ (tbl.GetForTarget (targetId), nullptr);
}

TEST_F (FighterTableTests, ProcessWithAttacks)
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
  tbl.ProcessWithAttacks ([&] (FighterTable::Handle f)
    {
      ++cnt;
      switch (cnt)
        {
        case 1:
          EXPECT_EQ (f->GetIdAsTarget ().type (),
                     proto::TargetId::TYPE_BUILDING);
          EXPECT_EQ (f->GetIdAsTarget ().id (), idBuilding);
          break;

        case 2:
          EXPECT_EQ (f->GetIdAsTarget ().type (),
                     proto::TargetId::TYPE_CHARACTER);
          EXPECT_EQ (f->GetIdAsTarget ().id (), idChar);
          break;

        default:
          FAIL () << "Too many regen-able fighters returned";
          break;
        }
    });
  EXPECT_EQ (cnt, 2);
}

TEST_F (FighterTableTests, ProcessForRegen)
{
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  buildings.CreateNew ("checkmark", "domob", Faction::GREEN);
  characters.CreateNew ("domob", Faction::GREEN);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idChar = c->GetId ();
  c->MutableHP ().set_shield (2);
  c->MutableRegenData ().mutable_max_hp ()->set_shield (10);
  c->MutableRegenData ().mutable_regeneration_mhp ()->set_shield (1);
  c.reset ();

  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto idBuilding = b->GetId ();
  b->MutableHP ().set_shield (2);
  b->MutableRegenData ().mutable_max_hp ()->set_shield (10);
  b->MutableRegenData ().mutable_regeneration_mhp ()->set_shield (1);
  b.reset ();

  unsigned cnt = 0;
  tbl.ProcessForRegen ([&] (FighterTable::Handle f)
    {
      ++cnt;
      switch (cnt)
        {
        case 1:
          EXPECT_EQ (f->GetIdAsTarget ().type (),
                     proto::TargetId::TYPE_BUILDING);
          EXPECT_EQ (f->GetIdAsTarget ().id (), idBuilding);
          break;

        case 2:
          EXPECT_EQ (f->GetIdAsTarget ().type (),
                     proto::TargetId::TYPE_CHARACTER);
          EXPECT_EQ (f->GetIdAsTarget ().id (), idChar);
          break;

        default:
          FAIL () << "Too many regen-able fighters returned";
          break;
        }
    });
  EXPECT_EQ (cnt, 2);
}

TEST_F (FighterTableTests, ProcessWithTarget)
{
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  buildings.CreateNew ("checkmark", "domob", Faction::GREEN);
  characters.CreateNew ("domob", Faction::GREEN);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idChar = c->GetId ();
  proto::TargetId t;
  t.set_id (5);
  c->SetTarget (t);
  c.reset ();

  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto idBuilding = b->GetId ();
  t.set_id (42);
  b->SetTarget (t);
  b.reset ();

  unsigned cnt = 0;
  tbl.ProcessWithTarget ([&] (FighterTable::Handle f)
    {
      ++cnt;
      switch (cnt)
        {
        case 1:
          EXPECT_EQ (f->GetIdAsTarget ().type (),
                     proto::TargetId::TYPE_BUILDING);
          EXPECT_EQ (f->GetIdAsTarget ().id (), idBuilding);
          break;

        case 2:
          EXPECT_EQ (f->GetIdAsTarget ().type (),
                     proto::TargetId::TYPE_CHARACTER);
          EXPECT_EQ (f->GetIdAsTarget ().id (), idChar);
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
