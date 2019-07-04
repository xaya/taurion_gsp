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

#include "fighter.hpp"

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

  /** Character table instance used in testing.  */
  CharacterTable characters;

  /** FighterTable used for testing.  */
  FighterTable tbl;

  FighterTests ()
    : characters(db), tbl(characters)
  {}

};

TEST_F (FighterTests, Characters)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (2, 5));
  c->MutableProto ().mutable_combat_data ()->add_attacks ();
  c->MutableHP ().set_armour (10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->MutableProto ().mutable_target ()->set_id (42);
  c.reset ();

  unsigned cnt = 0;
  tbl.ProcessAll ([this, id1, &cnt] (Fighter f)
    {
      ++cnt;

      switch (f.GetFaction ())
        {
        case Faction::RED:
          {
            EXPECT_EQ (f.GetPosition (), HexCoord (2, 5));
            EXPECT_EQ (f.GetCombatData ().attacks_size (), 1);

            const auto id = f.GetId ();
            EXPECT_EQ (id.type (), proto::TargetId::TYPE_CHARACTER);
            EXPECT_EQ (id.id (), id1);

            auto& hp = f.MutableHP ();
            EXPECT_EQ (hp.armour (), 10);
            hp.set_armour (5);

            proto::TargetId t;
            t.set_id (5);
            f.SetTarget (t);

            break;
          }

        case Faction::GREEN:
          EXPECT_EQ (f.GetTarget ().id (), 42);
          f.ClearTarget ();
          break;

        default:
          FAIL ()
              << "Unexpected faction: " << static_cast<int> (f.GetFaction ());
        }
    });
  EXPECT_EQ (cnt, 2);

  c = characters.GetById (id1);
  EXPECT_EQ (c->GetProto ().target ().id (), 5);
  EXPECT_EQ (c->GetHP ().armour (), 5);
  c.reset ();

  EXPECT_FALSE (characters.GetById (id2)->GetProto ().has_target ());
}

TEST_F (FighterTests, GetForTarget)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (HexCoord (42, -35));
  c.reset ();

  proto::TargetId targetId;
  targetId.set_type (proto::TargetId::TYPE_CHARACTER);
  targetId.set_id (id);

  auto f = tbl.GetForTarget (targetId);
  EXPECT_EQ (f.GetPosition (), HexCoord (42, -35));
  f.MutableHP ().set_shield (42);
  f.reset ();

  targetId.set_id (100);
  EXPECT_TRUE (tbl.GetForTarget (targetId).empty ());

  c = characters.GetById (id);
  EXPECT_EQ (c->GetHP ().shield (), 42);
  c.reset ();
}

TEST_F (FighterTests, ProcessWithTarget)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  c->MutableProto ().mutable_target ()->set_id (5);
  c.reset ();

  characters.CreateNew ("domob", Faction::GREEN);

  unsigned cnt = 0;
  tbl.ProcessWithTarget ([this, &cnt] (Fighter f)
    {
      ++cnt;
      EXPECT_EQ (f.GetFaction (), Faction::RED);
    });
  EXPECT_EQ (cnt, 1);
}

} // anonymous namespace
} // namespace pxd
