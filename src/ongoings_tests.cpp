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

#include "services.hpp"
#include "testutils.hpp"

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/inventory.hpp"
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

  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;
  CharacterTable characters;
  OngoingsTable ongoings;

  TestRandom rnd;
  ContextForTesting ctx;

  OngoingsTests ()
    : buildings(db), buildingInv(db), characters(db), ongoings(db)
  {}

  /**
   * Inserts an ongoing operation into the table, associated to the
   * given character.  Returns the handle for further changes.
   */
  OngoingsTable::Handle
  AddOp (Character& c)
  {
    auto op = ongoings.CreateNew (1);
    op->SetCharacterId (c.GetId ());
    c.MutableProto ().set_ongoing (op->GetId ());
    return op;
  }

  /**
   * Inserts an ongoing operation into the table, associated to the given
   * building.  Returns the handle.
   */
  OngoingsTable::Handle
  AddOp (Building& b)
  {
    auto op = ongoings.CreateNew (1);
    op->SetBuildingId (b.GetId ());
    return op;
  }

  /**
   * Returns the number of ongoing operations.
   */
  unsigned
  GetNumOngoing ()
  {
    auto res = ongoings.QueryAll ();
    unsigned cnt = 0;
    while (res.Step ())
      ++cnt;
    return cnt;
  }

};

TEST_F (OngoingsTests, ProcessedByHeight)
{
  proto::BlueprintCopy cpTemplate;
  cpTemplate.set_account ("domob");
  cpTemplate.set_original_type ("bow bpo");
  cpTemplate.set_num_copies (1);

  auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
  const auto bId = b->GetId ();

  auto op = AddOp (*b);
  op->SetHeight (10);
  cpTemplate.set_copy_type ("bow bpc");
  *op->MutableProto ().mutable_blueprint_copy () = cpTemplate;
  op.reset ();

  op = AddOp (*b);
  op->SetHeight (15);
  cpTemplate.set_copy_type ("sword bpc");
  *op->MutableProto ().mutable_blueprint_copy () = cpTemplate;
  op.reset ();

  b.reset ();

  ctx.SetHeight (9);
  ProcessAllOngoings (db, rnd, ctx);
  auto inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("sword bpc"), 0);
  EXPECT_EQ (GetNumOngoing (), 2);

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);
  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("sword bpc"), 0);
  EXPECT_EQ (GetNumOngoing (), 1);

  ctx.SetHeight (14);
  ProcessAllOngoings (db, rnd, ctx);
  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("sword bpc"), 0);
  EXPECT_EQ (GetNumOngoing (), 1);

  ctx.SetHeight (15);
  ProcessAllOngoings (db, rnd, ctx);
  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 2);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("sword bpc"), 1);
  EXPECT_EQ (GetNumOngoing (), 0);
}

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

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);

  c = characters.GetById (cId);
  EXPECT_FALSE (c->IsBusy ());
  EXPECT_EQ (c->GetHP ().armour (), 1'000);
  EXPECT_EQ (ongoings.GetById (opId), nullptr);
  EXPECT_EQ (GetNumOngoing (), 0);
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
  EXPECT_EQ (GetNumOngoing (), 0);
}

TEST_F (OngoingsTests, BlueprintCopy)
{
  const unsigned baseDuration = GetBpCopyBlocks ("bow bpc", ctx);

  auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
  const auto bId = b->GetId ();
  auto op = AddOp (*b);
  const auto opId = op->GetId ();
  op->SetHeight (baseDuration);
  auto& cp = *op->MutableProto ().mutable_blueprint_copy ();
  cp.set_account ("domob");
  cp.set_original_type ("bow bpo");
  cp.set_copy_type ("bow bpc");
  cp.set_num_copies (20);
  op.reset ();
  b.reset ();

  auto inv = buildingInv.Get (bId, "domob");
  inv->GetInventory ().AddFungibleCount ("bow bpc", 10);
  inv.reset ();

  /* The operation will be processed 20 times and produce a copy each time.  */
  for (unsigned i = 1; i < 20; ++i)
    {
      ctx.SetHeight (i * baseDuration);
      ProcessAllOngoings (db, rnd, ctx);

      inv = buildingInv.Get (bId, "domob");
      EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 0);
      EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 10 + i);

      ASSERT_EQ (GetNumOngoing (), 1);
      ASSERT_EQ (ongoings.GetById (opId)->GetHeight (), (i + 1) * baseDuration);
    }

  /* The final step will refund the original as well.  */
  ctx.SetHeight (20 * baseDuration);
  ProcessAllOngoings (db, rnd, ctx);
  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 30);
  EXPECT_EQ (GetNumOngoing (), 0);
}

TEST_F (OngoingsTests, ItemConstructionFromOriginal)
{
  const unsigned baseDuration = GetConstructionBlocks ("bow", ctx);

  auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
  const auto bId = b->GetId ();
  auto op = AddOp (*b);
  const auto opId = op->GetId ();
  op->SetHeight (baseDuration);
  auto& c = *op->MutableProto ().mutable_item_construction ();
  c.set_account ("domob");
  c.set_output_type ("bow");
  c.set_num_items (20);
  c.set_original_type ("bow bpo");
  op.reset ();
  b.reset ();

  auto inv = buildingInv.Get (bId, "domob");
  inv->GetInventory ().AddFungibleCount ("bow bpo", 10);
  inv.reset ();

  /* The operation will be processed 20 times (once for each item), and
     produce the items one by one.  */
  for (unsigned i = 1; i < 20; ++i)
    {
      ctx.SetHeight (i * baseDuration);
      ProcessAllOngoings (db, rnd, ctx);

      inv = buildingInv.Get (bId, "domob");
      EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 10);
      EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 0);
      EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow"), i);

      ASSERT_EQ (GetNumOngoing (), 1);
      ASSERT_EQ (ongoings.GetById (opId)->GetHeight (), (i + 1) * baseDuration);
    }

  /* The final construction step will clear out the ongoing operation
     and refund the bpo.  */
  ctx.SetHeight (20 * baseDuration);
  ProcessAllOngoings (db, rnd, ctx);
  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 11);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow"), 20);
  EXPECT_EQ (GetNumOngoing (), 0);
}

TEST_F (OngoingsTests, ItemConstructionFromCopy)
{
  auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
  const auto bId = b->GetId ();
  auto op = AddOp (*b);
  op->SetHeight (10);
  auto& c = *op->MutableProto ().mutable_item_construction ();
  c.set_account ("domob");
  c.set_output_type ("bow");
  c.set_num_items (5);
  op.reset ();
  b.reset ();

  auto inv = buildingInv.Get (bId, "domob");
  inv->GetInventory ().AddFungibleCount ("bow", 10);
  inv.reset ();

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);

  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow"), 15);
  EXPECT_EQ (GetNumOngoing (), 0);
}

TEST_F (OngoingsTests, BuildingConstruction)
{
  auto b = buildings.CreateNew ("huesli", "domob", Faction::RED);
  const auto bId = b->GetId ();
  b->MutableProto ().set_foundation (true);
  b->MutableHP ().set_armour (1);
  Inventory cInv(*b->MutableProto ().mutable_construction_inventory ());
  cInv.AddFungibleCount ("foo", 5);
  cInv.AddFungibleCount ("bar", 42);
  cInv.AddFungibleCount ("zerospace", 10);

  auto op = AddOp (*b);
  op->SetHeight (10);
  op->MutableProto ().mutable_building_construction ();

  op.reset ();
  b.reset ();

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);

  b = buildings.GetById (bId);
  auto inv = buildingInv.Get (bId, "domob");
  EXPECT_FALSE (b->GetProto ().foundation ());
  EXPECT_FALSE (b->GetProto ().has_construction_inventory ());
  EXPECT_EQ (b->GetHP ().armour (), 100);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("foo"), 2);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bar"), 42);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("zerospace"), 0);
  EXPECT_EQ (GetNumOngoing (), 0);
}

} // anonymous namespace
} // namespace pxd
