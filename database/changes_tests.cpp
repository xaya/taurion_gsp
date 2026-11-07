/*
    GSP for the Taurion blockchain game
    Copyright (C) 2026  Autonomous Worlds Ltd

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

#include "changes.hpp"

#include "account.hpp"
#include "building.hpp"
#include "character.hpp"
#include "damagelists.hpp"
#include "dbtest.hpp"
#include "dex.hpp"
#include "inventory.hpp"
#include "itemcounts.hpp"
#include "ongoing.hpp"
#include "region.hpp"

#include <xayautil/hash.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using testing::ElementsAre;
using testing::IsEmpty;

class ChangedKeysTests : public DBTestWithUndo
{

protected:

  ChangedKeys keys;

  AccountsTable accounts;
  BuildingsTable buildings;
  CharacterTable characters;
  DexOrderTable orders;

  ChangedKeysTests ()
    : accounts(db), buildings(db), characters(db), orders(db)
  {}

  bool
  ReadSince (const xaya::uint256& block)
  {
    keys = ChangedKeys ();
    return keys.ReadSince (db, block, height);
  }

};

TEST_F (ChangedKeysTests, UnknownBlock)
{
  EmptyBlock ();
  EXPECT_FALSE (ReadSince (xaya::SHA256::Hash ("unknown")));
}

TEST_F (ChangedKeysTests, Entities)
{
  accounts.CreateNew ("before");
  const auto from = Block ([this] ()
    {
      accounts.CreateNew ("also before");
    });

  Block ([this] ()
    {
      accounts.CreateNew ("domob");
      characters.CreateNew ("domob", Faction::RED);
      buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
      OngoingsTable (db).CreateNew (1);
      RegionsTable (db, height).GetById (42)
          ->MutableProto ().set_prospecting_character (1);
      GroundLootTable (db).GetByCoord (HexCoord (1, -2))
          ->GetInventory ().AddFungibleCount ("foo", 1);
      ItemCounts (db).IncrementFound ("foo");
    });
  Block ([this] ()
    {
      characters.DeleteById (1);
      characters.CreateNew ("domob", Faction::RED);
      GroundLootTable (db).GetByCoord (HexCoord (1, -2))
          ->GetInventory ().AddFungibleCount ("foo", -1);
    });

  ASSERT_TRUE (ReadSince (from));
  EXPECT_THAT (keys.accounts, ElementsAre ("domob"));
  EXPECT_THAT (keys.characters, ElementsAre (1, 4));
  EXPECT_THAT (keys.buildings, ElementsAre (2));
  EXPECT_THAT (keys.ongoings, ElementsAre (3));
  EXPECT_THAT (keys.regions, ElementsAre (42));
  EXPECT_THAT (keys.groundLoot, ElementsAre (HexCoord (1, -2)));

  ASSERT_TRUE (ReadSince (EmptyBlock ()));
  EXPECT_THAT (keys.accounts, IsEmpty ());
  EXPECT_THAT (keys.characters, IsEmpty ());
}

TEST_F (ChangedKeysTests, DataShownWithOtherEntities)
{
  orders.CreateNew (10, "bidder", DexOrder::Type::BID, "foo", 5, 2);
  orders.CreateNew (11, "seller", DexOrder::Type::ASK, "foo", 5, 2);
  const auto from = EmptyBlock ();

  Block ([this] ()
    {
      DamageLists (db, height).AddEntry (5, 6);
      BuildingInventoriesTable (db).Get (7, "andy")
          ->GetInventory ().AddFungibleCount ("foo", 1);

      orders.GetById (1)->ReduceQuantity (1);
      orders.GetById (2)->Delete ();
    });

  ASSERT_TRUE (ReadSince (from));
  EXPECT_THAT (keys.characters, ElementsAre (5));
  EXPECT_THAT (keys.buildings, ElementsAre (7, 10, 11));
  EXPECT_THAT (keys.accounts, ElementsAre ("bidder", "seller"));
}

TEST_F (ChangedKeysTests, MovementSteps)
{
  for (int i = 0; i < 3; ++i)
    characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (i, 0));
  const auto from = EmptyBlock ();

  Block ([this] ()
    {
      auto c = characters.GetById (1);
      c->SetPosition (HexCoord (1, 1));
      c->MutableVolatileMv ().set_partial_step (10);
      c.reset ();

      characters.GetById (2)->MutableVolatileMv ().set_partial_step (20);

      /* Like a teleport, this writes the full row but changes only
         the position.  */
      c = characters.GetById (3);
      c->MutableProto ().clear_movement ();
      c->SetPosition (HexCoord (5, 5));
    });

  ASSERT_TRUE (ReadSince (from));
  EXPECT_THAT (keys.characters, ElementsAre (1, 2, 3));
  EXPECT_THAT (keys.movedCharacters, ElementsAre (1, 2, 3));
  EXPECT_THAT (keys.combatCharacters, IsEmpty ());
}

TEST_F (ChangedKeysTests, CombatChanges)
{
  for (int i = 0; i < 7; ++i)
    characters.CreateNew ("domob", Faction::RED);
  auto c = characters.GetById (4);
  c->MutableRegenData ().mutable_regeneration_mhp ()->set_shield (100);
  c->MutableRegenData ().mutable_max_hp ()->set_shield (10);
  c->MutableHP ().set_shield (10);
  c.reset ();
  characters.GetById (6)->MutableEffects ().set_mentecon (true);
  const auto from = EmptyBlock ();

  Block ([this] ()
    {
      characters.ClearAllEffects ();
      characters.GetById (1)->MutableHP ().set_armour (5);

      proto::TargetId t;
      t.set_id (42);
      characters.GetById (2)->SetTarget (t);

      characters.GetById (3)->MutableEffects ().mutable_speed ()
          ->set_percent (10);

      /* This changes canregen as well.  */
      characters.GetById (4)->MutableHP ().set_shield (5);

      characters.GetById (5)->SetFriendlyTargets (true);

      /* canregen alone does not change with the API.  */
      (*db).Execute ("UPDATE `characters` SET `canregen` = 1 WHERE `id` = 7");
    });

  ASSERT_TRUE (ReadSince (from));
  EXPECT_THAT (keys.characters, ElementsAre (1, 2, 3, 4, 5, 6, 7));
  EXPECT_THAT (keys.movedCharacters, IsEmpty ());
  EXPECT_THAT (keys.combatCharacters, ElementsAre (1, 2, 3, 4, 5, 6, 7));
}

TEST_F (ChangedKeysTests, DamageListChanges)
{
  DamageLists (db, height).AddEntry (1, 10);
  DamageLists (db, height).AddEntry (2, 12);
  const auto from = EmptyBlock ();

  Block ([this] ()
    {
      DamageLists dl(db, height);
      dl.AddEntry (1, 10);
      dl.AddEntry (3, 10);
      dl.RemoveCharacter (12);
    });

  ASSERT_TRUE (ReadSince (from));
  EXPECT_THAT (keys.characters, ElementsAre (1, 2, 3));
  EXPECT_THAT (keys.movedCharacters, IsEmpty ());
  EXPECT_THAT (keys.combatCharacters, ElementsAre (1, 2, 3));
}

TEST_F (ChangedKeysTests, MovedAndCombat)
{
  for (int i = 0; i < 3; ++i)
    characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (i, 0));
  const auto from = EmptyBlock ();

  Block ([this] ()
    {
      auto c = characters.GetById (1);
      c->SetPosition (HexCoord (1, 1));
      c->MutableHP ().set_armour (5);
      c.reset ();

      proto::TargetId t;
      t.set_id (42);
      c = characters.GetById (2);
      c->MutableVolatileMv ().set_partial_step (10);
      c->SetTarget (t);
      c.reset ();

      characters.GetById (3)->MutableVolatileMv ().set_partial_step (10);
      DamageLists (db, height).AddEntry (3, 1);
    });

  ASSERT_TRUE (ReadSince (from));
  EXPECT_THAT (keys.characters, ElementsAre (1, 2, 3));
  EXPECT_THAT (keys.movedCharacters, ElementsAre (1, 2, 3));
  EXPECT_THAT (keys.combatCharacters, ElementsAre (1, 2, 3));
}

TEST_F (ChangedKeysTests, OtherCharacterChanges)
{
  for (int i = 0; i < 6; ++i)
    characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (i, 0));
  const auto from = EmptyBlock ();

  Block ([this] ()
    {
      for (Database::IdT id = 1; id <= 6; ++id)
        {
          auto c = characters.GetById (id);
          c->MutableVolatileMv ().set_partial_step (10);
          c->MutableHP ().set_armour (5);
        }

      characters.GetById (1)->MutableProto ().set_speed (100);
      characters.GetById (2)->SetBuildingId (100);
      characters.GetById (3)->MutableRegenData ().mutable_max_hp ()
          ->set_armour (10);
      characters.GetById (4)->SetEnterBuilding (100);
      characters.DeleteById (5);
      characters.CreateNew ("domob", Faction::RED);
    });

  ASSERT_TRUE (ReadSince (from));
  EXPECT_THAT (keys.characters, ElementsAre (1, 2, 3, 4, 5, 6, 7));
  EXPECT_THAT (keys.movedCharacters, ElementsAre (6));
  EXPECT_THAT (keys.combatCharacters, ElementsAre (6));
}

TEST_F (ChangedKeysTests, ChangesInAllBlocks)
{
  for (int i = 0; i < 3; ++i)
    characters.CreateNew ("domob", Faction::RED);
  const auto from = EmptyBlock ();

  const auto middle = Block ([this] ()
    {
      characters.GetById (1)->MutableVolatileMv ().set_partial_step (10);
      characters.GetById (2)->MutableHP ().set_armour (5);
      characters.GetById (3)->MutableProto ().set_speed (100);
      characters.CreateNew ("domob", Faction::RED);
    });
  Block ([this] ()
    {
      characters.GetById (1)->MutableHP ().set_armour (5);
      characters.GetById (2)->MutableVolatileMv ().set_partial_step (10);
      characters.GetById (3)->MutableHP ().set_armour (5);
      characters.GetById (4)->MutableVolatileMv ().set_partial_step (10);
    });

  ASSERT_TRUE (ReadSince (from));
  EXPECT_THAT (keys.characters, ElementsAre (1, 2, 3, 4));
  EXPECT_THAT (keys.movedCharacters, ElementsAre (1, 2));
  EXPECT_THAT (keys.combatCharacters, ElementsAre (1, 2));

  ASSERT_TRUE (ReadSince (middle));
  EXPECT_THAT (keys.characters, ElementsAre (1, 2, 3, 4));
  EXPECT_THAT (keys.movedCharacters, ElementsAre (2, 4));
  EXPECT_THAT (keys.combatCharacters, ElementsAre (1, 3));
}

TEST_F (ChangedKeysTests, TooManyBlocks)
{
  const auto from = EmptyBlock ();
  for (unsigned i = 0; i < 1'000; ++i)
    EmptyBlock ();
  EXPECT_TRUE (ReadSince (from));

  EmptyBlock ();
  EXPECT_FALSE (ReadSince (from));
}

TEST_F (ChangedKeysTests, InconsistentUndoData)
{
  const auto from = EmptyBlock ();
  ++height;
  EmptyBlock ();
  EXPECT_FALSE (ReadSince (from));
}

} // anonymous namespace
} // namespace pxd
