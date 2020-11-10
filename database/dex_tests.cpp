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

#include "dex.hpp"

#include "dbtest.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using testing::ElementsAre;
using testing::Pair;

/* ************************************************************************** */

class DexOrderTests : public DBTestWithSchema
{

protected:

  DexOrderTable orders;

  DexOrderTests ()
    : orders(db)
  {}

};

TEST_F (DexOrderTests, Creation)
{
  auto o = orders.CreateNew (123, "domob", DexOrder::Type::BID, "sword",
                             10, 100);
  const auto id1 = o->GetId ();
  o.reset ();

  o = orders.CreateNew (456, "andy", DexOrder::Type::ASK, "bow", 1, 255);
  const auto id2 = o->GetId ();
  o.reset ();

  o = orders.GetById (id1);
  EXPECT_EQ (o->GetBuilding (), 123);
  EXPECT_EQ (o->GetAccount (), "domob");
  EXPECT_EQ (o->GetType (), DexOrder::Type::BID);
  EXPECT_EQ (o->GetItem (), "sword");
  EXPECT_EQ (o->GetQuantity (), 10);
  EXPECT_EQ (o->GetPrice (), 100);

  o = orders.GetById (id2);
  EXPECT_EQ (o->GetBuilding (), 456);
  EXPECT_EQ (o->GetAccount (), "andy");
  EXPECT_EQ (o->GetType (), DexOrder::Type::ASK);
  EXPECT_EQ (o->GetItem (), "bow");
  EXPECT_EQ (o->GetQuantity (), 1);
  EXPECT_EQ (o->GetPrice (), 255);
}

TEST_F (DexOrderTests, QuantityReduction)
{
  auto o = orders.CreateNew (123, "domob", DexOrder::Type::BID, "sword",
                             10, 100);
  const auto id = o->GetId ();
  o->ReduceQuantity (2);
  o.reset ();

  o = orders.GetById (id);
  EXPECT_EQ (o->GetQuantity (), 8);
  o->ReduceQuantity (5);
  o.reset ();

  o = orders.GetById (id);
  EXPECT_EQ (o->GetQuantity (), 3);
  o->ReduceQuantity (3);
  o.reset ();

  EXPECT_EQ (orders.GetById (id), nullptr);
}

TEST_F (DexOrderTests, ExplicitDelete)
{
  db.SetNextId (101);

  auto o = orders.CreateNew (123, "domob", DexOrder::Type::BID, "sword",
                             10, 100);
  EXPECT_EQ (o->GetId (), 101);
  o.reset ();

  o = orders.CreateNew (42, "deleted", DexOrder::Type::ASK, "sword", 1, 2);
  EXPECT_EQ (o->GetId (), 102);
  o->Delete ();
  o.reset ();

  o = orders.CreateNew (456, "andy", DexOrder::Type::ASK, "bow", 1, 255);
  EXPECT_EQ (o->GetId (), 103);
  o.reset ();

  o = orders.GetById (103);
  o->Delete ();
  o.reset ();

  EXPECT_EQ (orders.GetById (102), nullptr);
  EXPECT_EQ (orders.GetById (103), nullptr);

  o = orders.GetById (101);
  ASSERT_NE (o, nullptr);
  EXPECT_EQ (o->GetAccount (), "domob");
  o.reset ();
}

/* ************************************************************************** */

class DexOrderTableTests : public DexOrderTests
{

protected:

  DexOrderTableTests ()
  {
    db.SetNextId (101);
  }

  /**
   * Expects that the result contains exactly the orders with the given
   * expected IDs (in order).
   */
  void
  ExpectOrderIds (Database::Result<DexOrderResult>&& res,
                  const std::vector<Database::IdT>& expectedIds)
  {
    for (const auto id : expectedIds)
      {
        ASSERT_TRUE (res.Step ()) << "Expected ID " << id << ", got end";
        ASSERT_EQ (orders.GetFromResult (res)->GetId (), id);
      }
    ASSERT_FALSE (res.Step ()) << "Expected end, but there are more results";
  }

};

TEST_F (DexOrderTableTests, QueryAll)
{
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 5, 10);
  orders.CreateNew (20, "andy", DexOrder::Type::ASK, "sword", 1, 20);
  orders.CreateNew (10, "andy", DexOrder::Type::BID, "sword", 1, 5);

  ExpectOrderIds (orders.QueryAll (), {101, 102, 103});
}

TEST_F (DexOrderTableTests, QueryForBuilding)
{
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 5, 10);
  orders.CreateNew (20, "andy", DexOrder::Type::ASK, "sword", 1, 20);
  orders.CreateNew (10, "andy", DexOrder::Type::BID, "sword", 1, 5);

  ExpectOrderIds (orders.QueryForBuilding (10), {103, 101});
  ExpectOrderIds (orders.QueryForBuilding (20), {102});
  ExpectOrderIds (orders.QueryForBuilding (42), {});
}

TEST_F (DexOrderTableTests, DeleteForBuilding)
{
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 5, 10);
  orders.CreateNew (20, "andy", DexOrder::Type::ASK, "sword", 1, 20);
  orders.CreateNew (10, "andy", DexOrder::Type::BID, "sword", 1, 5);

  orders.DeleteForBuilding (42);
  orders.DeleteForBuilding (10);

  EXPECT_EQ (orders.GetById (101), nullptr);
  EXPECT_NE (orders.GetById (102), nullptr);
  EXPECT_EQ (orders.GetById (103), nullptr);

  orders.DeleteForBuilding (20);
  EXPECT_EQ (orders.GetById (102), nullptr);
}

TEST_F (DexOrderTableTests, QueryToMatch)
{
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 1, 1);
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 1, 2);
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 1, 2);
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 1, 3);

  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "sword", 1, 30);
  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "sword", 1, 20);
  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "sword", 1, 20);
  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "sword", 1, 10);

  orders.CreateNew (20, "domob", DexOrder::Type::BID, "sword", 1, 5);
  orders.CreateNew (20, "domob", DexOrder::Type::ASK, "sword", 1, 6);

  orders.CreateNew (10, "domob", DexOrder::Type::BID, "bow", 1, 5);
  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "bow", 1, 6);

  ExpectOrderIds (orders.QueryToMatchBid (10, "sword", 20), {108, 106, 107});
  ExpectOrderIds (orders.QueryToMatchAsk (10, "sword", 2), {104, 102, 103});
}

TEST_F (DexOrderTableTests, ReservedCoins)
{
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 2, 3);
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "bow", 4, 1);
  orders.CreateNew (20, "domob", DexOrder::Type::BID, "sword", 1, 1);
  orders.CreateNew (10, "andy", DexOrder::Type::BID, "sword", 6, 7);
  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "sword", 10, 10);

  EXPECT_THAT (orders.GetReservedCoins (), ElementsAre (
    Pair ("andy", 42),
    Pair ("domob", 11)
  ));
  EXPECT_THAT (orders.GetReservedCoins (10), ElementsAre (
    Pair ("andy", 42),
    Pair ("domob", 10)
  ));
  EXPECT_THAT (orders.GetReservedCoins (20), ElementsAre (Pair ("domob", 1)));
  EXPECT_THAT (orders.GetReservedCoins (42), ElementsAre ());
}

TEST_F (DexOrderTableTests, ReservedQuantities)
{
  orders.CreateNew (10, "domob", DexOrder::Type::BID, "sword", 2, 3);
  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "sword", 2, 3);
  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "sword", 1, 5);
  orders.CreateNew (10, "andy", DexOrder::Type::ASK, "zerospace", 1, 1);
  orders.CreateNew (10, "domob", DexOrder::Type::ASK, "bow", 10, 1);
  orders.CreateNew (20, "domob", DexOrder::Type::ASK, "zerospace", 1, 1);

  Inventory invA, invB;
  invA.AddFungibleCount ("sword", 3);
  invA.AddFungibleCount ("bow", 10);
  invB.AddFungibleCount ("zerospace", 1);

  auto reserved = orders.GetReservedQuantities (10);
  ASSERT_EQ (reserved.size (), 2);
  EXPECT_EQ (reserved["domob"], invA);
  EXPECT_EQ (reserved["andy"], invB);

  reserved = orders.GetReservedQuantities (20);
  ASSERT_EQ (reserved.size (), 1);
  EXPECT_EQ (reserved["domob"], invB);

  EXPECT_TRUE (orders.GetReservedQuantities (42).empty ());
}

/* ************************************************************************** */

class DexHistoryTests : public DBTestWithSchema
{

protected:

  DexHistoryTable history;

  DexHistoryTests ()
    : history(db)
  {}

};

TEST_F (DexHistoryTests, RecordTrade)
{
  db.SetNextId (42);

  auto h = history.RecordTrade (10, 1'024, 3, "foo", 2, 3, "domob", "andy");
  EXPECT_EQ (h->GetHeight (), 10);
  EXPECT_EQ (h->GetTimestamp (), 1'024);
  EXPECT_EQ (h->GetBuilding (), 3);
  EXPECT_EQ (h->GetItem (), "foo");
  EXPECT_EQ (h->GetQuantity (), 2);
  EXPECT_EQ (h->GetPrice (), 3);
  EXPECT_EQ (h->GetSeller (), "domob");
  EXPECT_EQ (h->GetBuyer (), "andy");
  h.reset ();

  auto res = history.QueryForItem ("foo", 3);
  ASSERT_TRUE (res.Step ());
  h = history.GetFromResult (res);
  EXPECT_EQ (h->GetHeight (), 10);
  EXPECT_EQ (h->GetTimestamp (), 1'024);
  EXPECT_EQ (h->GetBuilding (), 3);
  EXPECT_EQ (h->GetItem (), "foo");
  EXPECT_EQ (h->GetQuantity (), 2);
  EXPECT_EQ (h->GetPrice (), 3);
  EXPECT_EQ (h->GetSeller (), "domob");
  EXPECT_EQ (h->GetBuyer (), "andy");
  h.reset ();
  EXPECT_FALSE (res.Step ());

  /* The history entries should not use up normal IDs.  */
  EXPECT_EQ (db.GetNextId (), 42);
}

TEST_F (DexHistoryTests, QueryForItem)
{
  history.RecordTrade (10, 1'024, 3, "foo", 1, 1, "domob", "andy");
  history.RecordTrade (10, 1'024, 4, "foo", 2, 2, "domob", "andy");
  history.RecordTrade (10, 1'024, 3, "bar", 3, 3, "domob", "andy");
  history.RecordTrade (9, 987, 3, "foo", 4, 4, "domob", "andy");

  EXPECT_FALSE (history.QueryForItem ("zerospace", 3).Step ());
  EXPECT_FALSE (history.QueryForItem ("foo", 42).Step ());

  auto res = history.QueryForItem ("foo", 3);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (history.GetFromResult (res)->GetQuantity (), 1);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (history.GetFromResult (res)->GetQuantity (), 4);
  EXPECT_FALSE (res.Step ());
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
