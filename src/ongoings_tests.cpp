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

#include "ongoings.hpp"

#include "testutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace pxd
{
namespace
{

class OngoingsTests : public DBTestWithSchema
{

protected:

  CharacterTable characters;
  OngoingsTable ongoings;

  TestRandom rnd;
  ContextForTesting ctx;

  OngoingsTests ()
    : characters(db), ongoings(db)
  {}

  /**
   * Inserts an ongoing operation into the table, associated to the
   * given character.  Returns the handle for further changes.
   */
  OngoingsTable::Handle
  AddOp (Character& c)
  {
    auto op = ongoings.CreateNew ();
    op->SetCharacterId (c.GetId ());
    c.MutableProto ().set_ongoing (op->GetId ());
    return op;
  }

};

/* FIXME: Once we have copying blueprints (which are easier as the existing
   character-based services), add a separate test just for the height
   filtering and perhaps remove the partial height testing from ArmourRepair
   afterwards.  */

TEST_F (OngoingsTests, ArmourRepair)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto cId = c->GetId ();
  c->MutableRegenData ().mutable_max_hp ()->set_armour (1'000);
  c->MutableHP ().set_armour (850);

  auto op = AddOp (*c);
  const auto opId = op->GetId ();
  op->SetHeight (10);
  op->MutableProto ().mutable_armour_repair ();

  op.reset ();
  c.reset ();

  ctx.SetHeight (9);
  ProcessAllOngoings (db, rnd, ctx);

  c = characters.GetById (cId);
  EXPECT_TRUE (c->IsBusy ());
  EXPECT_EQ (c->GetHP ().armour (), 850);
  EXPECT_NE (ongoings.GetById (opId), nullptr);
  c.reset ();

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);

  c = characters.GetById (cId);
  EXPECT_FALSE (c->IsBusy ());
  EXPECT_EQ (c->GetHP ().armour (), 1'000);
  EXPECT_EQ (ongoings.GetById (opId), nullptr);
}

TEST_F (OngoingsTests, Prospection)
{
  const HexCoord pos(5, 5);
  const auto region = ctx.Map ().Regions ().GetRegionId (pos);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto cId = c->GetId ();
  c->SetPosition (pos);

  auto op = AddOp (*c);
  op->SetHeight (10);
  op->MutableProto ().mutable_prospection ();

  op.reset ();
  c.reset ();

  RegionsTable regions(db, 5);
  regions.GetById (region)->MutableProto ().set_prospecting_character (cId);

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);

  c = characters.GetById (cId);
  EXPECT_FALSE (c->IsBusy ());
  auto r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "domob");
}

} // anonymous namespace
} // namespace pxd
