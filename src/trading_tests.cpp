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
  Parse (Account& a, const std::string& op)
  {
    return DexOperation::Parse (a, ParseJson (op),
                                ctx, accounts, buildings, buildingInv);
  }

protected:

  AccountsTable accounts;
  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;

  ContextForTesting ctx;

  DexOperationTests ()
    : accounts(db), buildings(db), buildingInv(db)
  {
    auto b = buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
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
    return Parse (*a, data) != nullptr;
  }

  /**
   * Parses an operation from JSON and validates it.  The format itself must
   * be valid.  The method returns false if the operation is invalid
   * (e.g. insufficient balance).  If it is valid, it is executed
   * and true returned.
   */
  bool
  Process (const std::string& name, const std::string& data)
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
   * Parses an operation from JSON and returns the associated pending JSON.
   * The operation must be valid format.
   */
  Json::Value
  GetPending (const std::string& data)
  {
    auto a = GetAccount ("pendingdummy");
    auto op = Parse (*a, data);
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

} // anonymous namespace
} // namespace pxd
