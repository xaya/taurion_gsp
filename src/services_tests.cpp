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

#include "services.hpp"

#include "testutils.hpp"

#include "database/dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class ServicesTests : public DBTestWithSchema
{

protected:

  /** ID of an ancient building with all services.  */
  static constexpr Database::IdT ANCIENT_BUILDING = 100;

  AccountsTable accounts;
  BuildingsTable buildings;
  BuildingInventoriesTable inv;

  ServicesTests ()
    : accounts(db), buildings(db), inv(db)
  {
    accounts.CreateNew ("domob", Faction::RED)->AddBalance (100);

    db.SetNextId (ANCIENT_BUILDING);
    auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
    CHECK_EQ (b->GetId (), ANCIENT_BUILDING);
    b->SetCentre (HexCoord (42, 10));

    /* We use refining for most general tests, thus it makes sense to set up
       basic resources for it already here.  */
    inv.Get (ANCIENT_BUILDING, "domob")
        ->GetInventory ().AddFungibleCount ("foo", 10);
  }

  /**
   * Calls TryServiceOperation with the given account and data parsed from
   * a JSON literal string.  Returns true if the operation was valid.
   */
  bool
  Process (const std::string& name, const std::string& dataStr)
  {
    auto a = accounts.GetByName (name);
    auto op = ServiceOperation::Parse (*a, ParseJson (dataStr), buildings, inv);

    if (op == nullptr)
      return false;

    op->Execute ();
    return true;
  }

};

TEST_F (ServicesTests, BasicOperation)
{
  ASSERT_TRUE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 3
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 90);
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("foo"), 7);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("bar"), 2);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("zerospace"), 1);
}

TEST_F (ServicesTests, InvalidFormat)
{
  EXPECT_FALSE (Process ("domob", "[]"));
  EXPECT_FALSE (Process ("domob", "null"));
  EXPECT_FALSE (Process ("domob", "\"foo\""));
  EXPECT_FALSE (Process ("domob", "{}"));

  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "i": "foo",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": "invalid",
    "i": "foo",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 42,
    "i": "foo",
    "n": 6
  })"));

  EXPECT_FALSE (Process ("domob", R"({
    "b": 100,
    "i": "foo",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "",
    "b": 100,
    "i": "foo",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "invalid type",
    "b": 100,
    "i": "foo",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": 42,
    "b": 100,
    "i": "foo",
    "n": 6
  })"));
}

TEST_F (ServicesTests, InvalidOperation)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 5
  })"));
}

TEST_F (ServicesTests, UnsupportedBuilding)
{
  db.SetNextId (200);
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  inv.Get (200, "domob")->GetInventory ().AddFungibleCount ("foo", 10);

  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 200,
    "i": "foo",
    "n": 3
  })"));
}

TEST_F (ServicesTests, InsufficientFunds)
{
  accounts.GetByName ("domob")->AddBalance (-91);

  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 3
  })"));
}

/* ************************************************************************** */

using RefiningTests = ServicesTests;

TEST_F (RefiningTests, InvalidFormat)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 3,
    "x": false
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": 42,
    "n": 3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": -3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": "x"
  })"));
}

TEST_F (RefiningTests, InvalidItemType)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "invalid item",
    "n": 3
  })"));
}

TEST_F (RefiningTests, ItemNotRefinable)
{
  inv.Get (ANCIENT_BUILDING, "domob")
      ->GetInventory ().AddFungibleCount ("bar", 10);

  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "bar",
    "n": 3
  })"));
}

TEST_F (RefiningTests, InvalidAmount)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": -3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 0
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 2
  })"));
}

TEST_F (RefiningTests, TooMuch)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 30
  })"));
}

TEST_F (RefiningTests, MultipleSteps)
{
  ASSERT_TRUE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 9
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 70);
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("foo"), 1);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("bar"), 6);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("zerospace"), 3);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
