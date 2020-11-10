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

#include "trading.hpp"

#include "jsonutils.hpp"
#include "testutils.hpp"

#include "database/dbtest.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class DexOperationTests : public DBTestWithSchema
{

private:

  /**
   * Returns an account handle for the given name, creating it if necessary.
   */
  AccountsTable::Handle
  GetAccount (const std::string& name)
  {
    auto a = accounts.GetByName (name);
    if (a == nullptr)
      return accounts.CreateNew (name);
    return a;
  }

  /**
   * Tries to parse an operation from JSON.  The operation handle is
   * returned without any further validation.
   */
  std::unique_ptr<DexOperation>
  Parse (Account& a, const Json::Value& op)
  {
    return DexOperation::Parse (a, op, ctx,
                                accounts, buildings, buildingInv, orders);
  }

protected:

  AccountsTable accounts;
  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;
  DexOrderTable orders;

  ContextForTesting ctx;

  DexOperationTests ()
    : accounts(db), buildings(db), buildingInv(db), orders(db)
  {
    accounts.CreateNew ("building")->SetFaction (Faction::RED);
    auto b = buildings.CreateNew ("checkmark", "building", Faction::RED);
    CHECK_EQ (b->GetId (), 1);
    b.reset ();
  }

  /**
   * Tries to parse an operation JSON and returns true/false depending
   * on whether it is well-formed (not taking specific validation
   * like balances into account).
   */
  bool
  IsValidFormat (const std::string& data)
  {
    auto a = GetAccount ("formatdummy");
    return Parse (*a, ParseJson (data)) != nullptr;
  }

  /**
   * Parses an operation from JSON and validates it.  The format itself must
   * be valid.  The method returns false if the operation is invalid
   * (e.g. insufficient balance).  If it is valid, it is executed
   * and true returned.
   */
  bool
  ProcessJson (const std::string& name, const Json::Value& data)
  {
    auto a = GetAccount (name);
    auto op = Parse (*a, data);
    CHECK (op != nullptr);

    if (!op->IsValid ())
      return false;

    op->Execute ();
    return true;
  }

  /**
   * Processes an operation as with ProcessJson, except that the operation
   * is taken directly from a JSON string.
   */
  bool
  Process (const std::string& name, const std::string& data)
  {
    return ProcessJson (name, ParseJson (data));
  }

  /**
   * Parses an operation from JSON and returns the associated pending JSON.
   * The operation must be valid format.
   */
  Json::Value
  GetPending (const std::string& data)
  {
    auto a = GetAccount ("pendingdummy");
    auto op = Parse (*a, ParseJson (data));
    CHECK (op != nullptr);
    return op->ToPendingJson ();
  }

  /**
   * Returns the amount of some item held in some building account.
   */
  Quantity
  ItemBalance (const Database::IdT building, const std::string& name,
               const std::string& item)
  {
    auto inv = buildingInv.Get (building, name);
    return inv->GetInventory ().GetFungibleCount (item);
  }

};

/* ************************************************************************** */

class DexTransferTests : public DexOperationTests
{

protected:

  DexTransferTests ()
  {
    buildingInv.Get (1, "domob")->GetInventory ().AddFungibleCount ("foo", 100);
  }

};

TEST_F (DexTransferTests, InvalidFormat)
{
  const std::string tests[] =
    {
      "42",
      "[]",
      R"({"b": 1, "i": "foo", "n": 5, "t": "andy", "x": 123})",
      R"({"b": "1", "i": "foo", "n": 5, "t": "andy"})",
      R"({"b": 1, "i": 42, "n": 5, "t": "andy"})",
      R"({"b": 1, "i": "foo", "n": "1", "t": "andy"})",
      R"({"b": 1, "i": "foo", "n": -1, "t": "andy"})",
      R"({"b": 1, "i": "foo", "n": 1.0, "t": "andy"})",
      R"({"b": 1, "i": "foo", "n": 0, "t": "andy"})",
      R"({"b": 1, "i": "foo", "n": 999999999999999999999, "t": "andy"})",
      R"({"b": 1, "i": "foo", "n": 5, "t": ["andy"]})",
      R"({"i": "foo", "n": 5, "t": "andy"})",
      R"({"b": 1, "n": 5, "t": "andy"})",
      R"({"b": 1, "i": "foo", "t": "andy"})",
      R"({"b": 1, "i": "foo", "n": 5})",
    };
  for (const auto& t : tests)
    EXPECT_FALSE (IsValidFormat (t)) << "Expected to be invalid:\n" << t;
}

TEST_F (DexTransferTests, InvalidItemOperation)
{
  db.SetNextId (101);
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT)
      ->MutableProto ().set_foundation (true);

  /* Invalid building (does not exist).  */
  EXPECT_FALSE (Process ("domob", R"({
    "b": 42,
    "i": "foo",
    "n": 1,
    "t": "andy"
  })"));

  /* Invalid building (is a foundation).  */
  EXPECT_FALSE (Process ("domob", R"({
    "b": 101,
    "i": "foo",
    "n": 1,
    "t": "andy"
  })"));

  /* Item does not exist.  */
  EXPECT_FALSE (Process ("domob", R"({
    "b": 1,
    "i": "invalid",
    "n": 1,
    "t": "andy"
  })"));
}

TEST_F (DexTransferTests, InsufficientBalance)
{
  EXPECT_FALSE (Process ("domob", R"({
    "b": 1,
    "i": "foo",
    "n": 101,
    "t": "andy"
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "b": 1,
    "i": "bar",
    "n": 1,
    "t": "andy"
  })"));
  EXPECT_FALSE (Process ("andy", R"({
    "b": 1,
    "i": "foo",
    "n": 1,
    "t": "domob"
  })"));
}

TEST_F (DexTransferTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPending (R"({
    "b": 1,
    "i": "foo",
    "n": 42,
    "t": "andy"
  })"), ParseJson (R"({
    "op": "transfer",
    "building": 1,
    "item": "foo",
    "num": 42,
    "to": "andy"
  })")));
}

TEST_F (DexTransferTests, Success)
{
  ASSERT_TRUE (Process ("domob", R"({
    "b": 1,
    "i": "foo",
    "n": 100,
    "t": "domob"
  })"));
  ASSERT_TRUE (Process ("domob", R"({
    "b": 1,
    "i": "foo",
    "n": 30,
    "t": "andy"
  })"));
  ASSERT_TRUE (Process ("andy", R"({
    "b": 1,
    "i": "foo",
    "n": 30,
    "t": "daniel"
  })"));

  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 70);
  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 0);
  EXPECT_EQ (ItemBalance (1, "daniel", "foo"), 30);
}

/* ************************************************************************** */

class NewOrderTests : public DexOperationTests
{

protected:

  NewOrderTests ()
  {
    accounts.CreateNew ("andy")->AddBalance (1'000);
    accounts.CreateNew ("domob")->AddBalance (1'000);

    buildingInv.Get (1, "andy")->GetInventory ().AddFungibleCount ("foo", 100);
    buildingInv.Get (1, "domob")->GetInventory ().AddFungibleCount ("foo", 100);

    /* We define some assets and orders in other buildings or for a different
       item, which none of the tests are supposed to touch.  */

    auto b = buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
    CHECK_EQ (b->GetId (), 2);
    b.reset ();

    buildingInv.Get (1, "domob")->GetInventory ().AddFungibleCount ("bar", 100);
    buildingInv.Get (2, "domob")->GetInventory ().AddFungibleCount ("foo", 100);

    db.SetNextId (11);
    orders.CreateNew (1, "domob", DexOrder::Type::BID, "bar", 1, 1'000);
    orders.CreateNew (1, "domob", DexOrder::Type::ASK, "bar", 1, 1);
    orders.CreateNew (2, "domob", DexOrder::Type::BID, "foo", 1, 1'000);
    orders.CreateNew (2, "domob", DexOrder::Type::ASK, "foo", 1, 1);
  }

  ~NewOrderTests ()
  {
    for (unsigned i = 11; i <= 14; ++i)
      EXPECT_NE (orders.GetById (i), nullptr) << "Order does not exist: " << i;

    EXPECT_EQ (ItemBalance (1, "domob", "bar"), 100);
    EXPECT_EQ (ItemBalance (2, "domob", "foo"), 100);
  }

  /**
   * Utility method to perform an order in our test building for the test item.
   */
  void
  PlaceOrder (const std::string& name, const DexOrder::Type t,
              const Quantity q, const Amount p)
  {
    auto op = ParseJson (R"({
      "b": 1,
      "i": "foo"
    })");

    op["n"] = IntToJson (q);
    switch (t)
      {
      case DexOrder::Type::BID:
        op["bp"] = IntToJson (p);
        break;
      case DexOrder::Type::ASK:
        op["ap"] = IntToJson (p);
        break;
      default:
        LOG (FATAL) << "Invalid order type: " << static_cast<int> (t);
      }

    EXPECT_TRUE (ProcessJson (name, op));
  }

};

TEST_F (NewOrderTests, InvalidFormat)
{
  const std::string tests[] =
    {
      "42",
      "[]",
      R"({"b": 1, "i": "foo", "n": 5, "t": "andy", "bp": 1})",
      R"({"b": 1, "i": "foo", "n": 5, "bp": "42"})",
      R"({"b": 1, "i": "foo", "n": 5, "bp": -5})",
      R"({"b": 1, "i": "foo", "n": 5, "bp": 100000000001})",
      R"({"b": 1, "i": "foo", "n": 5, "bp": 1, "ap": 2})",
      R"({"b": 1, "i": "foo", "n": 5, "ap": "42"})",
      R"({"b": 1, "i": "foo", "n": 5, "ap": -5})",
      R"({"b": 1, "i": "foo", "n": 5, "ap": 100000000001})",
      R"({"b": 1, "i": "foo", "n": 5, "ap": 1, "c": 42})",
    };
  for (const auto& t : tests)
    EXPECT_FALSE (IsValidFormat (t)) << "Expected to be invalid:\n" << t;

}
TEST_F (NewOrderTests, InvalidItemOperation)
{
  db.SetNextId (101);
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT)
      ->MutableProto ().set_foundation (true);

  /* Invalid building (does not exist).  */
  EXPECT_FALSE (Process ("domob", R"({
    "b": 42,
    "i": "foo",
    "n": 1,
    "bp": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "b": 42,
    "i": "foo",
    "n": 1,
    "ap": 1
  })"));

  /* Invalid building (is a foundation).  */
  EXPECT_FALSE (Process ("domob", R"({
    "b": 101,
    "i": "foo",
    "n": 1,
    "bp": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "b": 101,
    "i": "foo",
    "n": 1,
    "ap": 1
  })"));

  /* Item does not exist.  */
  EXPECT_FALSE (Process ("domob", R"({
    "b": 1,
    "i": "invalid",
    "n": 1,
    "bp": 1
  })"));
  EXPECT_FALSE (Process ("domob", R"({
    "b": 1,
    "i": "invalid",
    "n": 1,
    "ap": 1
  })"));
}

TEST_F (NewOrderTests, InsufficientBalance)
{
  /* Trying to sell more than 100 foo.  */
  EXPECT_FALSE (Process ("domob", R"({
    "b": 1,
    "i": "foo",
    "n": 101,
    "ap": 1
  })"));

  /* Offering more than 1k Cubits for foo (in total).  */
  EXPECT_FALSE (Process ("domob", R"({
    "b": 1,
    "i": "foo",
    "n": 10,
    "bp": 101
  })"));
}

TEST_F (NewOrderTests, PendingJson)
{
  EXPECT_TRUE (PartialJsonEqual (GetPending (R"({
    "b": 1,
    "i": "foo",
    "n": 42,
    "bp": 2
  })"), ParseJson (R"({
    "op": "bid",
    "building": 1,
    "item": "foo",
    "num": 42,
    "price": 2
  })")));

  EXPECT_TRUE (PartialJsonEqual (GetPending (R"({
    "b": 1,
    "i": "foo",
    "n": 42,
    "ap": 5
  })"), ParseJson (R"({
    "op": "ask",
    "building": 1,
    "item": "foo",
    "num": 42,
    "price": 5
  })")));
}

TEST_F (NewOrderTests, VeryHighAsk)
{
  /* Asks are valid as long as the price is not exceeding MAX_MONEY, even
     if the total cost is exceeding the money supply.  */
  db.SetNextId (101);
  ASSERT_TRUE (Process ("domob", R"({
    "b": 1,
    "i": "foo",
    "n": 10,
    "ap": 100000000000
  })"));

  auto o = orders.GetById (101);
  ASSERT_NE (o, nullptr);
  EXPECT_EQ (o->GetType (), DexOrder::Type::ASK);
  EXPECT_EQ (o->GetQuantity (), 10);
  EXPECT_EQ (o->GetPrice (), 100'000'000'000);

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 90);
}

/* ************************************************************************** */

class OrderMatchingTests : public NewOrderTests
{

protected:

  OrderMatchingTests ()
  {
    /* These orders are created directly in the table, so they won't
       reduce assets / balances of domob.  */
    db.SetNextId (101);
    orders.CreateNew (1, "domob", DexOrder::Type::BID, "foo", 10, 1);
    orders.CreateNew (1, "domob", DexOrder::Type::BID, "foo", 1, 3);
    orders.CreateNew (1, "domob", DexOrder::Type::ASK, "foo", 1, 10);
    orders.CreateNew (1, "domob", DexOrder::Type::ASK, "foo", 10, 20);

    /* We want to execute these tests without any DEX fees (there are
       separate unit tests for the fees).  Thus we set the building
       owner fee to -10%, which offsets the base fee on regtest completely.
       This obviously only works by changing the value directly, and won't
       be possible to do in the real game through moves.  */
    buildings.GetById (1)->MutableProto ().set_dex_fee_bps (-1'000);
    accounts.GetByName ("building")->AddBalance (1'000'000);

    /* Future orders (placed by the test) will have IDs from 201.  */
    db.SetNextId (201);
  }

};

TEST_F (OrderMatchingTests, NewBid)
{
  PlaceOrder ("andy", DexOrder::Type::BID, 2, 5);

  auto o = orders.GetById (201);
  ASSERT_NE (o, nullptr);
  EXPECT_EQ (o->GetType (), DexOrder::Type::BID);
  EXPECT_EQ (o->GetAccount (), "andy");
  EXPECT_EQ (o->GetBuilding (), 1);
  EXPECT_EQ (o->GetItem (), "foo");
  EXPECT_EQ (o->GetQuantity (), 2);
  EXPECT_EQ (o->GetPrice (), 5);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 990);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000);
  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 100);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 100);
}

TEST_F (OrderMatchingTests, NewAsk)
{
  PlaceOrder ("andy", DexOrder::Type::ASK, 2, 5);

  auto o = orders.GetById (201);
  ASSERT_NE (o, nullptr);
  EXPECT_EQ (o->GetType (), DexOrder::Type::ASK);
  EXPECT_EQ (o->GetAccount (), "andy");
  EXPECT_EQ (o->GetBuilding (), 1);
  EXPECT_EQ (o->GetItem (), "foo");
  EXPECT_EQ (o->GetQuantity (), 2);
  EXPECT_EQ (o->GetPrice (), 5);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000);
  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 98);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 100);
}

TEST_F (OrderMatchingTests, FilledBid)
{
  PlaceOrder ("andy", DexOrder::Type::BID, 2, 100);

  EXPECT_EQ (orders.GetById (101)->GetQuantity (), 10);
  EXPECT_EQ (orders.GetById (102)->GetQuantity (), 1);
  EXPECT_EQ (orders.GetById (103), nullptr);
  EXPECT_EQ (orders.GetById (104)->GetQuantity (), 9);
  EXPECT_EQ (db.GetNextId (), 201);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000 - 10 - 20);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000 + 10 + 20);
  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 102);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 100);
}

TEST_F (OrderMatchingTests, FilledAsk)
{
  PlaceOrder ("andy", DexOrder::Type::ASK, 2, 0);

  EXPECT_EQ (orders.GetById (101)->GetQuantity (), 9);
  EXPECT_EQ (orders.GetById (102), nullptr);
  EXPECT_EQ (orders.GetById (103)->GetQuantity (), 1);
  EXPECT_EQ (orders.GetById (104)->GetQuantity (), 10);
  EXPECT_EQ (db.GetNextId (), 201);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000 + 3 + 1);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000);
  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 98);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 102);
}

TEST_F (OrderMatchingTests, PartialBid)
{
  PlaceOrder ("andy", DexOrder::Type::BID, 2, 15);

  EXPECT_EQ (orders.GetById (101)->GetQuantity (), 10);
  EXPECT_EQ (orders.GetById (102)->GetQuantity (), 1);
  EXPECT_EQ (orders.GetById (103), nullptr);
  EXPECT_EQ (orders.GetById (104)->GetQuantity (), 10);
  EXPECT_EQ (orders.GetById (201)->GetQuantity (), 1);
  EXPECT_EQ (db.GetNextId (), 202);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000 - 10 - 15);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000 + 10);
  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 101);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 100);
}

TEST_F (OrderMatchingTests, PartialAsk)
{
  PlaceOrder ("andy", DexOrder::Type::ASK, 2, 2);

  EXPECT_EQ (orders.GetById (101)->GetQuantity (), 10);
  EXPECT_EQ (orders.GetById (102), nullptr);
  EXPECT_EQ (orders.GetById (103)->GetQuantity (), 1);
  EXPECT_EQ (orders.GetById (104)->GetQuantity (), 10);
  EXPECT_EQ (orders.GetById (201)->GetQuantity (), 1);
  EXPECT_EQ (db.GetNextId (), 202);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000 + 3);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000);
  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 98);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 101);
}

TEST_F (OrderMatchingTests, FillingOwnOrder)
{
  PlaceOrder ("domob", DexOrder::Type::ASK, 1, 3);
  PlaceOrder ("domob", DexOrder::Type::BID, 1, 10);

  EXPECT_EQ (orders.GetById (101)->GetQuantity (), 10);
  EXPECT_EQ (orders.GetById (102), nullptr);
  EXPECT_EQ (orders.GetById (103), nullptr);
  EXPECT_EQ (orders.GetById (104)->GetQuantity (), 10);
  EXPECT_EQ (db.GetNextId (), 201);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000 + 3);
  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 100);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 101);
}

/* ************************************************************************** */

class DexFeeTests : public NewOrderTests
{

protected:

  DexFeeTests ()
  {
    /* We also do tests with the building owner account here, to see
       how that interacts with the fee they get.  */
    accounts.GetByName ("building")->AddBalance (1'000);
    buildingInv.Get (1, "building")
        ->GetInventory ().AddFungibleCount ("foo", 1'000);

    /* Owner fee in the tests is 20%, for a total fee of 30%.  */
    buildings.GetById (1)->MutableProto ().set_dex_fee_bps (2'000);

    db.SetNextId (101);
  }

};

TEST_F (DexFeeTests, BasicFeeDistribution)
{
  PlaceOrder ("domob", DexOrder::Type::ASK, 1, 100);
  PlaceOrder ("andy", DexOrder::Type::BID, 2, 100);
  PlaceOrder ("domob", DexOrder::Type::ASK, 1, 100);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000 - 200);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000 + 140);
  EXPECT_EQ (accounts.GetByName ("building")->GetBalance (), 1'000 + 40);

  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 102);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 98);
}

TEST_F (DexFeeTests, ZeroPrice)
{
  PlaceOrder ("domob", DexOrder::Type::ASK, 1, 0);
  PlaceOrder ("andy", DexOrder::Type::BID, 2, 0);
  PlaceOrder ("domob", DexOrder::Type::ASK, 1, 0);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000);
  EXPECT_EQ (accounts.GetByName ("building")->GetBalance (), 1'000);

  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 102);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 98);
}

TEST_F (DexFeeTests, Rounding)
{
  for (unsigned i = 0; i < 10; ++i)
    PlaceOrder ("domob", DexOrder::Type::ASK, 1, 1);
  PlaceOrder ("andy", DexOrder::Type::BID, 10, 1);

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 1'000 - 10);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 1'000);
  EXPECT_EQ (accounts.GetByName ("building")->GetBalance (), 1'000);

  EXPECT_EQ (ItemBalance (1, "andy", "foo"), 110);
  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 90);
}

TEST_F (DexFeeTests, BuildingOwnerSells)
{
  PlaceOrder ("building", DexOrder::Type::ASK, 1'000, 1);

  /* Even though we get money back (and end up with sufficient balance),
     it is not possible to buy with more than what we have liquid.  */

  ASSERT_FALSE (Process ("building", R"({
    "b": 1,
    "i": "foo",
    "n": 1001,
    "bp": 1
  })"));

  PlaceOrder ("building", DexOrder::Type::BID, 1'000, 1);
  EXPECT_EQ (ItemBalance (1, "building", "foo"), 1'000);
  EXPECT_EQ (accounts.GetByName ("building")->GetBalance (), 1'000 - 100);
}

/* ************************************************************************** */

using CancelOrderTests = DexOperationTests;

TEST_F (CancelOrderTests, InvalidFormat)
{
  const std::string tests[] =
    {
      "42",
      "[]",
      "{}",
      R"({"c": "42"})",
      R"({"c": 0})",
      R"({"c": -5})",
      R"({"c": 1, "x": 2})",
    };
  for (const auto& t : tests)
    EXPECT_FALSE (IsValidFormat (t)) << "Expected to be invalid:\n" << t;
}

TEST_F (CancelOrderTests, NonExistingOrder)
{
  EXPECT_FALSE (Process ("domob", R"({"c": 42})"));
}

TEST_F (CancelOrderTests, OnlyOwnerCanCancel)
{
  db.SetNextId (101);
  orders.CreateNew (1, "domob", DexOrder::Type::BID, "foo", 1, 1);
  EXPECT_FALSE (Process ("andy", R"({"c": 101})"));
  EXPECT_TRUE (Process ("domob", R"({"c": 101})"));
}

TEST_F (CancelOrderTests, PendingJson)
{
  db.SetNextId (101);
  orders.CreateNew (1, "domob", DexOrder::Type::BID, "foo", 1, 1);
  EXPECT_TRUE (PartialJsonEqual (GetPending (R"({"c": 101})"), ParseJson (R"({
    "op": "cancel",
    "order": 101
  })")));
}

TEST_F (CancelOrderTests, CancelBid)
{
  db.SetNextId (101);
  orders.CreateNew (1, "domob", DexOrder::Type::BID, "foo", 2, 3);
  ASSERT_TRUE (Process ("domob", R"({"c": 101})"));

  EXPECT_EQ (orders.GetById (101), nullptr);
  EXPECT_EQ (db.GetNextId (), 102);

  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 0);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 6);
}

TEST_F (CancelOrderTests, CancelAsk)
{
  db.SetNextId (101);
  orders.CreateNew (1, "domob", DexOrder::Type::ASK, "foo", 2, 3);
  ASSERT_TRUE (Process ("domob", R"({"c": 101})"));

  EXPECT_EQ (orders.GetById (101), nullptr);
  EXPECT_EQ (db.GetNextId (), 102);

  EXPECT_EQ (ItemBalance (1, "domob", "foo"), 2);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 0);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
