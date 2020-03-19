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

private:

  ContextForTesting ctx;

protected:

  /** ID of an ancient building with all services.  */
  static constexpr Database::IdT ANCIENT_BUILDING = 100;

  AccountsTable accounts;
  BuildingsTable buildings;
  BuildingInventoriesTable inv;
  CharacterTable characters;

  ServicesTests ()
    : accounts(db), buildings(db), inv(db), characters(db)
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
    auto op = ServiceOperation::Parse (*a, ParseJson (dataStr),
                                       ctx, buildings, inv, characters);

    if (op == nullptr)
      return false;

    op->Execute ();
    return true;
  }

  /**
   * Parses the given operation and returns its associated pending JSON.
   */
  Json::Value
  GetPendingJson (const std::string& name, const std::string& dataStr)
  {
    auto a = accounts.GetByName (name);
    auto op = ServiceOperation::Parse (*a, ParseJson (dataStr),
                                       ctx, buildings, inv, characters);
    CHECK (op != nullptr);
    return op->ToPendingJson ();
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

TEST_F (ServicesTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPendingJson ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 6
  })"), ParseJson (R"({
    "building": 100,
    "cost": 20
  })")));
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

TEST_F (RefiningTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPendingJson ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 6
  })"), ParseJson (R"({
    "type": "refining",
    "input": {"foo": 6},
    "output": {"bar": 4, "zerospace": 2}
  })")));
}

/* ************************************************************************** */

class RepairTests : public ServicesTests
{

protected:

  RepairTests ()
  {
    db.SetNextId (200);
    auto c = characters.CreateNew ("domob", Faction::RED);
    c->SetBuildingId (ANCIENT_BUILDING);
    c->MutableRegenData ().mutable_max_hp ()->set_armour (1'000);
    c->MutableHP ().set_armour (950);
  }

};

TEST_F (RepairTests, InvalidFormat)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200,
    "x": false
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": "foo"
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": -10
  })"));
}

TEST_F (RepairTests, NonExistantCharacter)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 12345
  })"));
}

TEST_F (RepairTests, NonOwnedCharacter)
{
  accounts.CreateNew ("andy", Faction::RED)->AddBalance (100);
  EXPECT_FALSE (Process ("andy", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));
}

TEST_F (RepairTests, NotInBuilding)
{
  characters.GetById (200)->SetPosition (HexCoord (1, 2));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));

  characters.GetById (200)->SetBuildingId (5);
  EXPECT_FALSE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));
}

TEST_F (RepairTests, NoMissingHp)
{
  characters.GetById (200)->MutableHP ().set_armour (1'000);
  EXPECT_FALSE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));
}

TEST_F (RepairTests, BasicExecution)
{
  ASSERT_TRUE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));

  auto c = characters.GetById (200);
  EXPECT_EQ (c->GetBusy (), 1);
  EXPECT_TRUE (c->GetProto ().has_armour_repair ());
  EXPECT_EQ (c->GetHP ().armour (), 950);
  c.reset ();

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 95);
}

TEST_F (RepairTests, SingleHpMissing)
{
  characters.GetById (200)->MutableHP ().set_armour (999);
  ASSERT_TRUE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));

  EXPECT_EQ (characters.GetById (200)->GetBusy (), 1);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 99);
}

TEST_F (RepairTests, MultipleBlocks)
{
  characters.GetById (200)->MutableHP ().set_armour (100);
  ASSERT_TRUE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));

  EXPECT_EQ (characters.GetById (200)->GetBusy (), 9);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 10);
}

TEST_F (RepairTests, AlreadyRepairing)
{
  ASSERT_TRUE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));

  EXPECT_EQ (characters.GetById (200)->GetBusy (), 1);

  EXPECT_FALSE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));
}

TEST_F (RepairTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPendingJson ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"), ParseJson (R"({
    "type": "armourrepair",
    "character": 200
  })")));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
