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

#include <xayautil/hash.hpp>

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class ServicesTests : public DBTestWithSchema
{

private:

  /**
   * Parses an operation for the given account and from JSON.
   */
  std::unique_ptr<ServiceOperation>
  ParseOp (Account& a, const std::string& dataStr)
  {
    return ServiceOperation::Parse (a, ParseJson (dataStr),
                                    ctx, accounts, buildings,
                                    inv, characters, itemCounts, ongoings);
  }

protected:

  /** ID of an ancient building with all services.  */
  static constexpr Database::IdT ANCIENT_BUILDING = 100;

  TestRandom rnd;
  ContextForTesting ctx;

  AccountsTable accounts;
  BuildingsTable buildings;
  BuildingInventoriesTable inv;
  CharacterTable characters;
  ItemCounts itemCounts;
  OngoingsTable ongoings;

  ServicesTests ()
    : accounts(db), buildings(db), inv(db), characters(db),
      itemCounts(db), ongoings(db)
  {
    auto a = accounts.CreateNew ("domob");
    a->SetFaction (Faction::RED);
    a->AddBalance (100);
    a.reset ();

    db.SetNextId (ANCIENT_BUILDING);
    auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
    CHECK_EQ (b->GetId (), ANCIENT_BUILDING);
    b->SetCentre (HexCoord (42, 10));

    /* We use refining for most general tests, thus it makes sense to set up
       basic resources for it already here.  */
    inv.Get (ANCIENT_BUILDING, "domob")
        ->GetInventory ().AddFungibleCount ("test ore", 10);
  }

  /**
   * Tries to parse, validate and execute a service operation with the given
   * account and data parsed from a JSON literal string.  Returns true if the
   * operation was valid.
   */
  bool
  Process (const std::string& name, const std::string& dataStr)
  {
    auto a = accounts.GetByName (name);
    return Process (ParseOp (*a, dataStr));
  }

  /**
   * Validates and (if valid) executes a given service operation handle.
   * Returns true if it was valid and has been executed.
   */
  bool
  Process (std::unique_ptr<ServiceOperation> op)
  {
    if (op == nullptr || !op->IsFullyValid ())
      return false;

    op->Execute (rnd);
    return true;
  }

  /**
   * Parses the given operation and returns its associated pending JSON.
   */
  Json::Value
  GetPendingJson (const std::string& name, const std::string& dataStr)
  {
    auto a = accounts.GetByName (name);
    auto op = ParseOp (*a, dataStr);
    CHECK (op != nullptr);
    return op->ToPendingJson ();
  }

};

constexpr Database::IdT ServicesTests::ANCIENT_BUILDING;

TEST_F (ServicesTests, BasicOperation)
{
  ASSERT_TRUE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 3
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 90);
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("test ore"), 7);
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
    "i": "test ore",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": "invalid",
    "i": "test ore",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 42,
    "i": "test ore",
    "n": 6
  })"));

  EXPECT_FALSE (Process ("domob", R"({
    "b": 100,
    "i": "test ore",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "",
    "b": 100,
    "i": "test ore",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "invalid type",
    "b": 100,
    "i": "test ore",
    "n": 6
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": 42,
    "b": 100,
    "i": "test ore",
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
    "i": "test ore",
    "n": 5
  })"));
}

TEST_F (ServicesTests, UnsupportedBuilding)
{
  db.SetNextId (200);
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  buildings.CreateNew ("ancient1", "", Faction::ANCIENT)
      ->MutableProto ().set_foundation (true);
  inv.Get (200, "domob")->GetInventory ().AddFungibleCount ("test ore", 10);
  inv.Get (201, "domob")->GetInventory ().AddFungibleCount ("test ore", 10);

  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 200,
    "i": "test ore",
    "n": 3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 201,
    "i": "test ore",
    "n": 3
  })"));
}

TEST_F (ServicesTests, InsufficientFunds)
{
  accounts.GetByName ("domob")->AddBalance (-91);

  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 3
  })"));
}

TEST_F (ServicesTests, PendingJson)
{
  accounts.CreateNew ("andy")->SetFaction (Faction::RED);

  auto b = buildings.CreateNew ("ancient1", "andy", Faction::RED);
  ASSERT_EQ (b->GetId (), 101);
  b->MutableProto ().mutable_config ()->set_service_fee_percent (50);
  b.reset ();

  inv.Get (101, "domob")->GetInventory ().AddFungibleCount ("test ore", 10);

  EXPECT_TRUE (PartialJsonEqual (GetPendingJson ("domob", R"({
    "t": "ref",
    "b": 101,
    "i": "test ore",
    "n": 6
  })"), ParseJson (R"({
    "building": 101,
    "cost":
      {
        "base": 20,
        "fee": 10
      }
  })")));
}

/* ************************************************************************** */

class ServicesFeeTests : public ServicesTests
{

protected:

  ServicesFeeTests ()
  {
    /* For some fee tests, we need an account with just enough balance
       for the base cost.  This will be "andy" (as opposed to "domob" who
       has 100 coins).  */
    auto a = accounts.CreateNew ("andy");
    a->SetFaction (Faction::RED);
    a->AddBalance (10);
    a.reset ();

    CHECK_EQ (buildings.CreateNew ("ancient1", "andy", Faction::RED)
                ->GetId (), 101);

    inv.Get (ANCIENT_BUILDING, "andy")
        ->GetInventory ().AddFungibleCount ("test ore", 10);
    inv.Get (101, "andy")
        ->GetInventory ().AddFungibleCount ("test ore", 10);
    inv.Get (101, "domob")
        ->GetInventory ().AddFungibleCount ("test ore", 10);
  }

};

TEST_F (ServicesFeeTests, NoFeeInAncientBuilding)
{
  ASSERT_TRUE (Process ("andy", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 3
  })"));
  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 0);
}

TEST_F (ServicesFeeTests, NoFeeInOwnBuilding)
{
  buildings.GetById (101)
      ->MutableProto ().mutable_config ()->set_service_fee_percent (50);
  ASSERT_TRUE (Process ("andy", R"({
    "t": "ref",
    "b": 101,
    "i": "test ore",
    "n": 3
  })"));
  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 0);
}

TEST_F (ServicesFeeTests, InsufficientBalanceWithFee)
{
  auto b = buildings.CreateNew ("ancient1", "domob", Faction::RED);
  ASSERT_EQ (b->GetId (), 102);
  b->MutableProto ().mutable_config ()->set_service_fee_percent (50);
  b.reset ();

  inv.Get (102, "andy")
      ->GetInventory ().AddFungibleCount ("test ore", 10);

  ASSERT_FALSE (Process ("andy", R"({
    "t": "ref",
    "b": 102,
    "i": "test ore",
    "n": 3
  })"));
  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 10);
}

TEST_F (ServicesFeeTests, NormalFeePayment)
{
  buildings.GetById (101)
      ->MutableProto ().mutable_config ()->set_service_fee_percent (50);
  ASSERT_TRUE (Process ("domob", R"({
    "t": "ref",
    "b": 101,
    "i": "test ore",
    "n": 3
  })"));
  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 15);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 85);
}

TEST_F (ServicesFeeTests, ZeroFeePossible)
{
  buildings.GetById (101)
      ->MutableProto ().mutable_config ()->set_service_fee_percent (0);
  ASSERT_TRUE (Process ("domob", R"({
    "t": "ref",
    "b": 101,
    "i": "test ore",
    "n": 3
  })"));
  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 10);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 90);
}

TEST_F (ServicesFeeTests, FeeRoundedUp)
{
  buildings.GetById (101)
      ->MutableProto ().mutable_config ()->set_service_fee_percent (1);
  ASSERT_TRUE (Process ("domob", R"({
    "t": "ref",
    "b": 101,
    "i": "test ore",
    "n": 3
  })"));
  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 11);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 89);
}

/* ************************************************************************** */

using RefiningTests = ServicesTests;

TEST_F (RefiningTests, InvalidFormat)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
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
    "i": "test ore",
    "n": -3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 3.0
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
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
      ->GetInventory ().AddFungibleCount ("foo", 10);

  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "foo",
    "n": 3
  })"));
}

TEST_F (RefiningTests, InvalidAmount)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": -3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 0
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 2
  })"));
}

TEST_F (RefiningTests, TooMuch)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 30
  })"));
}

TEST_F (RefiningTests, MultipleSteps)
{
  ASSERT_TRUE (Process ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 9
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 70);
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("test ore"), 1);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("bar"), 6);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("zerospace"), 3);
}

TEST_F (RefiningTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPendingJson ("domob", R"({
    "t": "ref",
    "b": 100,
    "i": "test ore",
    "n": 6
  })"), ParseJson (R"({
    "type": "refining",
    "input": {"test ore": 6},
    "output": {"bar": 4, "zerospace": 2}
  })")));
}

class MobileRefiningTests : public RefiningTests
{

private:

  /**
   * Parses a JSON string into an operation and returns it.
   */
  std::unique_ptr<ServiceOperation>
  ParseOp (Account& a, const std::string& dataStr)
  {
    return ServiceOperation::ParseMobileRefining (
              a, *character, ParseJson (dataStr),
              ctx, accounts, inv, itemCounts, ongoings);
  }

protected:

  /**
   * The character used in tests to do the refining with.  By default it
   * has a mobile refinery, but tests may want to disable it instead.
   */
  CharacterTable::Handle character;

  MobileRefiningTests ()
  {
    character = characters.CreateNew ("domob", Faction::RED);
    character->MutableProto ()
        .mutable_refining ()->mutable_input ()->set_percent (100);

    /* Also add some test ore for simplicity.  */
    character->GetInventory ().AddFungibleCount ("test ore", 20);
  }

  /**
   * Tries to parse and process a given refining operation from JSON.
   */
  bool
  Process (const std::string& dataStr)
  {
    auto a = accounts.GetByName (character->GetOwner ());
    return RefiningTests::Process (ParseOp (*a, dataStr));
  }

  /**
   * Returns the pending JSON of the operation parsed from JSON.
   */
  Json::Value
  GetPendingJson (const std::string& dataStr)
  {
    auto a = accounts.GetByName (character->GetOwner ());
    auto op = ParseOp (*a, dataStr);
    CHECK (op != nullptr);
    return op->ToPendingJson ();
  }

};

TEST_F (MobileRefiningTests, InvalidFormat)
{
  EXPECT_FALSE (Process (R"([1, 2, 3])"));
  EXPECT_FALSE (Process (R"("test")"));
  EXPECT_FALSE (Process (R"({})"));
  EXPECT_FALSE (Process (R"({
    "x": "foo",
    "i": "test ore",
    "n": 6
  })"));
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": 6.0
  })"));
  EXPECT_FALSE (Process (R"({
    "i": 42,
    "n": 6
  })"));
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": "6"
  })"));
  EXPECT_FALSE (Process (R"({
    "i": "test ore"
  })"));
  EXPECT_FALSE (Process (R"({
    "n": 3
  })"));
}

TEST_F (MobileRefiningTests, MultipleSteps)
{
  ASSERT_TRUE (Process (R"({
    "i": "test ore",
    "n": 18
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 70);
  EXPECT_EQ (character->GetInventory ().GetFungibleCount ("test ore"), 2);
  EXPECT_EQ (character->GetInventory ().GetFungibleCount ("bar"), 6);
  EXPECT_EQ (character->GetInventory ().GetFungibleCount ("zerospace"), 3);
}

TEST_F (MobileRefiningTests, RefiningNotSupported)
{
  character->MutableProto ().clear_refining ();
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": 6
  })"));
}

TEST_F (MobileRefiningTests, InsufficientFunds)
{
  accounts.GetByName (character->GetOwner ())->AddBalance (-91);
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": 6
  })"));
}

TEST_F (MobileRefiningTests, InvalidOrUnsupportedItem)
{
  character->GetInventory ().AddFungibleCount ("foo", 20);
  EXPECT_FALSE (Process (R"({
    "i": "invalid item",
    "n": 6
  })"));
  EXPECT_FALSE (Process (R"({
    "i": "foo",
    "n": 6
  })"));
}

TEST_F (MobileRefiningTests, InvalidAmount)
{
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": -3
  })"));
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": 0
  })"));
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": 10
  })"));
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": 3
  })"));
}

TEST_F (MobileRefiningTests, TooMuch)
{
  EXPECT_FALSE (Process (R"({
    "i": "test ore",
    "n": 60
  })"));
}

TEST_F (MobileRefiningTests, PendingJson)
{
  ASSERT_EQ (character->GetId (), 101);
  EXPECT_TRUE (PartialJsonEqual (GetPendingJson (R"({
    "i": "test ore",
    "n": 6
  })"), ParseJson (R"({
    "building": null,
    "character": 101,
    "cost":
      {
        "base": 10,
        "fee": 0
      },
    "type": "refining",
    "input": {"test ore": 6},
    "output": {"bar": 2, "zerospace": 1}
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

    ctx.SetHeight (100);
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
  auto a = accounts.CreateNew ("andy");
  a->SetFaction (Faction::RED);
  a->AddBalance (100);
  a.reset ();

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
  ASSERT_TRUE (c->IsBusy ());
  EXPECT_EQ (c->GetHP ().armour (), 950);

  auto op = ongoings.GetById (c->GetProto ().ongoing ());
  EXPECT_EQ (op->GetHeight (), 101);
  EXPECT_EQ (op->GetCharacterId (), c->GetId ());
  EXPECT_TRUE (op->GetProto ().has_armour_repair ());

  op.reset ();
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

  auto c = characters.GetById (200);
  ASSERT_TRUE (c->IsBusy ());
  auto op = ongoings.GetById (c->GetProto ().ongoing ());
  EXPECT_EQ (op->GetHeight (), 101);
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

  auto c = characters.GetById (200);
  ASSERT_TRUE (c->IsBusy ());
  auto op = ongoings.GetById (c->GetProto ().ongoing ());
  EXPECT_EQ (op->GetHeight (), 109);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 10);
}

TEST_F (RepairTests, AlreadyRepairing)
{
  ASSERT_TRUE (Process ("domob", R"({
    "t": "fix",
    "b": 100,
    "c": 200
  })"));

  auto c = characters.GetById (200);
  ASSERT_TRUE (c->IsBusy ());
  auto op = ongoings.GetById (c->GetProto ().ongoing ());
  EXPECT_EQ (op->GetHeight (), 101);

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

class RevEngTests : public ServicesTests
{

protected:

  RevEngTests ()
  {
    inv.Get (ANCIENT_BUILDING, "domob")
        ->GetInventory ().AddFungibleCount ("test artefact", 3);
  }

};

TEST_F (RevEngTests, InvalidFormat)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": 1,
    "x": false
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": 42,
    "n": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": -1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": "x"
  })"));
}

TEST_F (RevEngTests, InvalidItemType)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "invalid item",
    "n": 1
  })"));
}

TEST_F (RevEngTests, ItemNotAnArtefact)
{
  inv.Get (ANCIENT_BUILDING, "domob")
      ->GetInventory ().AddFungibleCount ("foo", 10);

  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "foo",
    "n": 1
  })"));
}

TEST_F (RevEngTests, InvalidAmount)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": -3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": 0
  })"));
}

TEST_F (RevEngTests, TooMuch)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": 30
  })"));
}

TEST_F (RevEngTests, OneItem)
{
  ASSERT_TRUE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": 1
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 90);
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("test artefact"), 2);
  const auto bow = i->GetInventory ().GetFungibleCount ("bow bpo");
  const auto sword = i->GetInventory ().GetFungibleCount ("sword bpo");
  const auto red = i->GetInventory ().GetFungibleCount ("red fitment bpo");
  EXPECT_EQ (bow + sword + red, 1);
  EXPECT_EQ (itemCounts.GetFound ("bow bpo"), bow);
  EXPECT_EQ (itemCounts.GetFound ("sword bpo"), sword);
  EXPECT_EQ (itemCounts.GetFound ("red fitment bpo"), red);
}

TEST_F (RevEngTests, ManyTries)
{
  constexpr unsigned bowOffset = 10;

  accounts.GetByName ("domob")->AddBalance (1'000'000);
  inv.Get (ANCIENT_BUILDING, "domob")
      ->GetInventory ().AddFungibleCount ("test artefact", 1'000);
  for (unsigned i = 0; i < bowOffset; ++i)
    itemCounts.IncrementFound ("bow bpo");

  ASSERT_TRUE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": 1000
  })"));

  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  const auto bow = i->GetInventory ().GetFungibleCount ("bow bpo");
  const auto sword = i->GetInventory ().GetFungibleCount ("sword bpo");
  const auto red = i->GetInventory ().GetFungibleCount ("red fitment bpo");
  LOG (INFO)
      << "Found " << bow << " bows, "
      << sword << " swords and "
      << red << " red-only fitments";
  EXPECT_GT (bow, 0);
  EXPECT_GT (sword, bow);
  EXPECT_GT (red, 0);
  EXPECT_EQ (itemCounts.GetFound ("bow bpo"), bow + bowOffset);
  EXPECT_EQ (itemCounts.GetFound ("sword bpo"), sword);
  EXPECT_EQ (itemCounts.GetFound ("red fitment bpo"), red);
}

TEST_F (RevEngTests, FactionRestriction)
{
  /* A green account should not be able to get the red-only fitment
     from reverse engineering, even with many tries.  Also it should
     use up exactly one random number per try, and not e.g. do re-rolls
     when picking one fitment that isn't available.  */

  constexpr unsigned trials = 1'000;

  auto a = accounts.CreateNew ("green");
  a->SetFaction (Faction::GREEN);
  a->AddBalance (1'000'000);
  a.reset ();

  inv.Get (ANCIENT_BUILDING, "green")
      ->GetInventory ().AddFungibleCount ("test artefact", trials);

  rnd.Seed (xaya::SHA256::Hash ("foo"));
  ASSERT_TRUE (Process ("green", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": 1000
  })"));

  auto i = inv.Get (ANCIENT_BUILDING, "green");
  const auto bow = i->GetInventory ().GetFungibleCount ("bow bpo");
  const auto sword = i->GetInventory ().GetFungibleCount ("sword bpo");
  const auto red = i->GetInventory ().GetFungibleCount ("red fitment bpo");
  LOG (INFO)
      << "Found " << bow << " bows, "
      << sword << " swords and "
      << red << " red-only fitments";
  EXPECT_GT (bow, 0);
  EXPECT_GT (sword, 0);
  EXPECT_EQ (red, 0);

  const auto actualNext = rnd.Next<uint64_t> ();
  rnd.Seed (xaya::SHA256::Hash ("foo"));
  for (unsigned i = 0; i < trials; ++i)
    {
      rnd.NextInt (100);
      rnd.ProbabilityRoll (1, 1'000);
    }
  EXPECT_EQ (actualNext, rnd.Next<uint64_t> ())
      << "Wrong number of random numbers used for reverse engineering";
}

TEST_F (RevEngTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPendingJson ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "test artefact",
    "n": 2
  })"), ParseJson (R"({
    "type": "reveng",
    "input": {"test artefact": 2}
  })")));
}

/* ************************************************************************** */

class BlueprintCopyTests : public ServicesTests
{

protected:

  BlueprintCopyTests ()
  {
    accounts.GetByName ("domob")->AddBalance (999'900);
    CHECK_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000'000);
    inv.Get (ANCIENT_BUILDING, "domob")
        ->GetInventory ().AddFungibleCount ("sword bpo", 1);

    ctx.SetHeight (100);
  }

};

TEST_F (BlueprintCopyTests, InvalidFormat)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "sword bpo",
    "n": 1,
    "x": false
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": 42,
    "n": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "sword bpo",
    "n": -1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "rve",
    "b": 100,
    "i": "sword bpo",
    "n": "x"
  })"));
}

TEST_F (BlueprintCopyTests, InvalidItemType)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "invalid item",
    "n": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "sword",
    "n": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "sword bpc",
    "n": 1
  })"));
}

TEST_F (BlueprintCopyTests, InvalidAmount)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "sword bpo",
    "n": -3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "sword bpo",
    "n": 0
  })"));
}

TEST_F (BlueprintCopyTests, NotOwned)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "bow bpo",
    "n": 1
  })"));
}

TEST_F (BlueprintCopyTests, Success)
{
  db.SetNextId (100);
  ASSERT_TRUE (Process ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "sword bpo",
    "n": 10
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 999'000);
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("sword bpo"), 0);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("sword bpc"), 0);

  auto op = ongoings.GetById (100);
  ASSERT_NE (op, nullptr);
  EXPECT_EQ (op->GetHeight (), 100 + 10);
  EXPECT_EQ (op->GetBuildingId (), ANCIENT_BUILDING);
  ASSERT_TRUE (op->GetProto ().has_blueprint_copy ());
  const auto& cp = op->GetProto ().blueprint_copy ();
  EXPECT_EQ (cp.account (), "domob");
  EXPECT_EQ (cp.original_type (), "sword bpo");
  EXPECT_EQ (cp.copy_type (), "sword bpc");
  EXPECT_EQ (cp.num_copies (), 10);
}

TEST_F (BlueprintCopyTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPendingJson ("domob", R"({
    "t": "cp",
    "b": 100,
    "i": "sword bpo",
    "n": 2
  })"), ParseJson (R"({
    "type": "bpcopy",
    "original": "sword bpo",
    "output": {"sword bpc": 2}
  })")));
}

/* ************************************************************************** */

class ConstructionTests : public ServicesTests
{

protected:

  ConstructionTests ()
  {
    accounts.GetByName ("domob")->AddBalance (999'900);
    CHECK_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000'000);

    auto i = inv.Get (ANCIENT_BUILDING, "domob");
    i->GetInventory ().AddFungibleCount ("sword bpo", 1);
    i->GetInventory ().AddFungibleCount ("sword bpc", 1);
    i->GetInventory ().AddFungibleCount ("zerospace", 100);

    ctx.SetHeight (100);
  }

};

TEST_F (ConstructionTests, InvalidFormat)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword bpo",
    "n": 1,
    "x": false
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": 42,
    "n": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword bpo",
    "n": -1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword bpo",
    "n": "x"
  })"));
}

TEST_F (ConstructionTests, InvalidItemType)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "invalid item",
    "n": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword",
    "n": 1
  })"));
}

TEST_F (ConstructionTests, InvalidAmount)
{
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword bpo",
    "n": -3
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword bpo",
    "n": 0
  })"));
}

TEST_F (ConstructionTests, MissingResources)
{
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  i->GetInventory ().AddFungibleCount ("bow bpo", 1);
  i->GetInventory ().AddFungibleCount ("foo", 100);
  i->GetInventory ().AddFungibleCount ("bar", 2);
  i.reset ();

  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "bow bpo",
    "n": 3
  })"));
}

TEST_F (ConstructionTests, MissingBlueprints)
{
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  i->GetInventory ().AddFungibleCount ("bow bpc", 1);
  i->GetInventory ().AddFungibleCount ("foo", 100);
  i->GetInventory ().AddFungibleCount ("bar", 200);
  i.reset ();

  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "bow bpo",
    "n": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "bow bpc",
    "n": 2
  })"));
}

TEST_F (ConstructionTests, FactionRestrictions)
{
  auto a = accounts.CreateNew ("green");
  a->SetFaction (Faction::GREEN);
  a->AddBalance (1'000'000);
  a.reset ();

  auto i = inv.Get (ANCIENT_BUILDING, "green");
  i->GetInventory ().AddFungibleCount ("foo", 10);
  i->GetInventory ().AddFungibleCount ("red fitment bpo", 1);
  i = inv.Get (ANCIENT_BUILDING, "domob");
  i->GetInventory ().AddFungibleCount ("foo", 10);
  i->GetInventory ().AddFungibleCount ("red fitment bpo", 1);
  i.reset ();

  EXPECT_FALSE (Process ("green", R"({
    "t": "bld",
    "b": 100,
    "i": "red fitment bpo",
    "n": 1
  })"));
  EXPECT_TRUE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "red fitment bpo",
    "n": 1
  })"));
}

TEST_F (ConstructionTests, RequiredServiceType)
{
  inv.Get (ANCIENT_BUILDING, "domob")
      ->GetInventory ().AddFungibleCount ("chariot bpo", 1);

  db.SetNextId (201);
  buildings.CreateNew ("itemmaker", "", Faction::ANCIENT);
  buildings.CreateNew ("carmaker", "", Faction::ANCIENT);

  for (const Database::IdT id : {201, 202})
    {
      auto i = inv.Get (id, "domob");
      i->GetInventory ().AddFungibleCount ("sword bpo", 1);
      i->GetInventory ().AddFungibleCount ("chariot bpo", 1);
      i->GetInventory ().AddFungibleCount ("zerospace", 10);
    }

  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 201,
    "i": "chariot bpo",
    "n": 1
  })"));
  EXPECT_TRUE (Process ("domob", R"({
    "t": "bld",
    "b": 201,
    "i": "sword bpo",
    "n": 1
  })"));

  EXPECT_FALSE (Process ("domob", R"({
    "t": "bld",
    "b": 202,
    "i": "sword bpo",
    "n": 1
  })"));
  EXPECT_TRUE (Process ("domob", R"({
    "t": "bld",
    "b": 202,
    "i": "chariot bpo",
    "n": 1
  })"));
}

TEST_F (ConstructionTests, FromOriginal)
{
  db.SetNextId (100);
  ASSERT_TRUE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword bpo",
    "n": 5
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 999'500);
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("sword bpo"), 0);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("sword bpc"), 1);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("zerospace"), 50);

  auto op = ongoings.GetById (100);
  ASSERT_NE (op, nullptr);
  EXPECT_EQ (op->GetHeight (), 100 + 10);
  EXPECT_EQ (op->GetBuildingId (), ANCIENT_BUILDING);
  ASSERT_TRUE (op->GetProto ().has_item_construction ());
  const auto& c = op->GetProto ().item_construction ();
  EXPECT_EQ (c.account (), "domob");
  EXPECT_EQ (c.output_type (), "sword");
  EXPECT_EQ (c.num_items (), 5);
  EXPECT_EQ (c.original_type (), "sword bpo");
}

TEST_F (ConstructionTests, FromCopy)
{
  inv.Get (ANCIENT_BUILDING, "domob")
      ->GetInventory ().AddFungibleCount ("sword bpc", 4);
  db.SetNextId (100);
  ASSERT_TRUE (Process ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword bpc",
    "n": 5
  })"));

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 999'500);
  auto i = inv.Get (ANCIENT_BUILDING, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("sword bpo"), 1);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("sword bpc"), 0);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("zerospace"), 50);

  auto op = ongoings.GetById (100);
  ASSERT_NE (op, nullptr);
  EXPECT_EQ (op->GetHeight (), 100 + 10);
  EXPECT_EQ (op->GetBuildingId (), ANCIENT_BUILDING);
  ASSERT_TRUE (op->GetProto ().has_item_construction ());
  const auto& c = op->GetProto ().item_construction ();
  EXPECT_EQ (c.account (), "domob");
  EXPECT_EQ (c.output_type (), "sword");
  EXPECT_EQ (c.num_items (), 5);
  EXPECT_FALSE (c.has_original_type ());
}

TEST_F (ConstructionTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPendingJson ("domob", R"({
    "t": "bld",
    "b": 100,
    "i": "sword bpo",
    "n": 2
  })"), ParseJson (R"({
    "type": "construct",
    "blueprint": "sword bpo",
    "output": {"sword": 2}
  })")));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
