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

#include <sqlite3.h>

#include <string>

namespace pxd
{
namespace
{

using testing::ElementsAre;
using testing::IsEmpty;

class ChangedKeysTests : public DBTestWithSchema
{

protected:

  /** Height of the last block.  */
  unsigned height = 10;

  ChangedKeys keys;

  AccountsTable accounts;
  BuildingsTable buildings;
  CharacterTable characters;
  DexOrderTable orders;

  ChangedKeysTests ()
    : accounts(db), buildings(db), characters(db), orders(db)
  {
    (*db).Execute (R"(
      CREATE TABLE `xayagame_undo`
          (`hash` BLOB PRIMARY KEY,
           `data` BLOB NOT NULL,
           `height` INTEGER NOT NULL);
    )");
  }

  /**
   * Runs the given function as the state update of a new block, and stores
   * its changeset as undo data like libxayagame does.  Returns the block hash.
   */
  template <typename Fcn>
    xaya::uint256
    Block (const Fcn& update)
  {
    std::string changeset;
    (*db).AccessDatabase ([&] (sqlite3* h)
      {
        sqlite3_session* session;
        CHECK_EQ (sqlite3session_create (h, "main", &session), SQLITE_OK);
        CHECK_EQ (sqlite3session_attach (session, nullptr), SQLITE_OK);

        update ();

        int size;
        void* data;
        CHECK_EQ (sqlite3session_changeset (session, &size, &data), SQLITE_OK);
        changeset.assign (static_cast<const char*> (data), size);
        sqlite3_free (data);
        sqlite3session_delete (session);
      });

    ++height;
    const auto hash = xaya::SHA256::Hash (std::to_string (height));

    auto stmt = (*db).Prepare (R"(
      INSERT INTO `xayagame_undo` (`hash`, `data`, `height`)
        VALUES (?1, ?2, ?3)
    )");
    stmt.Bind (1, hash);
    stmt.BindBlob (2, changeset);
    stmt.Bind (3, height);
    stmt.Execute ();

    return hash;
  }

  /**
   * Adds a block without any changes.
   */
  xaya::uint256
  EmptyBlock ()
  {
    return Block ([] () {});
  }

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
