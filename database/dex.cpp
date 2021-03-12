/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

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

#include <glog/logging.h>

namespace pxd
{

/* ************************************************************************** */

DexOrder::DexOrder (Database& d)
  : db(d), id(db.GetNextId ()),
    tracker(db.TrackHandle ("dex order", id)),
    buildingId(Database::EMPTY_ID),
    type(Type::INVALID),
    quantity(0), price(0),
    isNew(true), dirty(false)
{
  VLOG (1) << "Created new DEX order with ID " << id;
}

DexOrder::DexOrder (Database& d, const Database::Result<DexOrderResult>& res)
  : db(d), isNew(false), dirty(false)
{
  id = res.Get<DexOrderResult::id> ();
  tracker = db.TrackHandle ("dex order", id);

  buildingId = res.Get<DexOrderResult::building> ();
  account = res.Get<DexOrderResult::account> ();
  type = static_cast<Type> (res.Get<DexOrderResult::type> ());
  CHECK (type == Type::BID || type == Type::ASK)
      << "Unexpected order type read from DB for order " << id
      << ": " << static_cast<int> (type);
  item = res.Get<DexOrderResult::item> ();
  quantity = res.Get<DexOrderResult::quantity> ();
  price = res.Get<DexOrderResult::price> ();
}

DexOrder::~DexOrder ()
{
  if (isNew && quantity == 0)
    {
      VLOG (1) << "Not inserting immediately deleted order " << id;
      return;
    }

  if (isNew)
    {
      VLOG (1) << "Inserting new DEX order " << id << " into the database";

      CHECK_NE (buildingId, Database::EMPTY_ID)
          << "No building ID set for new order " << id;
      CHECK_NE (item, "")
          << "No item type set for new order " << id;

      CHECK (type == Type::BID || type == Type::ASK)
          << "Unexpected order type for DB insertion for order " << id
          << ": " << static_cast<int> (type);

      CHECK_GT (quantity, 0)
          << "No quantity set for order " << id;
      CHECK_LE (quantity, MAX_QUANTITY)
          << "Invalid quantity for new order " << id;
      CHECK_GE (price, 0)
          << "Invalid (negative) price for order " << id;

      auto stmt = db.Prepare (R"(
        INSERT INTO `dex_orders`
          (`id`, `building`, `account`, `type`, `item`, `quantity`, `price`)
          VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
      )");

      stmt.Bind (1, id);
      stmt.Bind (2, buildingId);
      stmt.Bind (3, account);
      stmt.Bind (4, static_cast<int> (type));
      stmt.Bind (5, item);
      stmt.Bind (6, quantity);
      stmt.Bind (7, price);

      stmt.Execute ();
      return;
    }

  if (!dirty)
    {
      VLOG (1) << "DEX order " << id << " is not dirty";
      return;
    }

  if (quantity == 0)
    {
      VLOG (1) << "Deleting used up order " << id;
      auto stmt = db.Prepare (R"(
        DELETE FROM `dex_orders`
          WHERE `id` = ?1
      )");
      stmt.Bind (1, id);
      stmt.Execute ();
      return;
    }

  VLOG (1) << "Updating dirty DEX order " << id;
  CHECK_GT (quantity, 0)
      << "Invalid item quantity for updated order " << id;

  auto stmt = db.Prepare (R"(
    UPDATE `dex_orders`
      SET `quantity` = ?2
      WHERE `id` = ?1
  )");
  stmt.Bind (1, id);
  stmt.Bind (2, quantity);
  stmt.Execute ();
}

void
DexOrder::ReduceQuantity (const Quantity q)
{
  CHECK_LE (q, quantity);
  quantity -= q;
  dirty = true;
}

void
DexOrder::Delete ()
{
  quantity = 0;
  dirty = true;
}

/* ************************************************************************** */

DexOrderTable::Handle
DexOrderTable::CreateNew (const Database::IdT building, const std::string& acc,
                          const DexOrder::Type type, const std::string& item,
                          const Quantity quantity, const Amount price)
{
  Handle o(new DexOrder (db));

  o->buildingId = building;
  o->account = acc;
  o->type = type;
  o->item = item;
  o->quantity = quantity;
  o->price = price;

  return o;
}

DexOrderTable::Handle
DexOrderTable::GetFromResult (const Database::Result<DexOrderResult>& res)
{
  return Handle (new DexOrder (db, res));
}

DexOrderTable::Handle
DexOrderTable::GetById (const Database::IdT id)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `dex_orders`
      WHERE `id` = ?1
  )");
  stmt.Bind (1, id);
  auto res = stmt.Query<DexOrderResult> ();
  if (!res.Step ())
    return nullptr;

  auto o = GetFromResult (res);
  CHECK (!res.Step ());
  return o;
}

Database::Result<DexOrderResult>
DexOrderTable::QueryAll ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `dex_orders`
      ORDER BY `id`
  )");
  return stmt.Query<DexOrderResult> ();
}

Database::Result<DexOrderResult>
DexOrderTable::QueryForBuilding (const Database::IdT building)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `dex_orders`
      WHERE `building` = ?1
      ORDER BY `item`, `type`, `price`, `id`
  )");
  stmt.Bind (1, building);
  return stmt.Query<DexOrderResult> ();
}

Database::Result<DexOrderResult>
DexOrderTable::QueryToMatchBid (const Database::IdT building,
                                const std::string& item, const Amount price)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `dex_orders`
      WHERE
        `building` = ?1 AND `item` = ?2 AND `type` = ?3
        AND `price` <= ?4
      ORDER BY `price`, `id`
  )");
  stmt.Bind (1, building);
  stmt.Bind (2, item);
  stmt.Bind (3, static_cast<int> (DexOrder::Type::ASK));
  stmt.Bind (4, price);
  return stmt.Query<DexOrderResult> ();
}

Database::Result<DexOrderResult>
DexOrderTable::QueryToMatchAsk (const Database::IdT building,
                                const std::string& item, const Amount price)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `dex_orders`
      WHERE
        `building` = ?1 AND `item` = ?2 AND `type` = ?3
        AND `price` >= ?4
      ORDER BY `price` DESC, `id`
  )");
  stmt.Bind (1, building);
  stmt.Bind (2, item);
  stmt.Bind (3, static_cast<int> (DexOrder::Type::BID));
  stmt.Bind (4, price);
  return stmt.Query<DexOrderResult> ();
}

namespace
{

/**
 * Database result with reserved Cubit balances per account.
 */
struct ReservedCoinsResult : public Database::ResultType
{
  RESULT_COLUMN (std::string, account, 1);
  RESULT_COLUMN (int64_t, cost, 2);
};

/**
 * Database result with reserved item quantities per account.
 */
struct ReservedQuantitiesResult : public Database::ResultType
{
  RESULT_COLUMN (std::string, account, 1);
  RESULT_COLUMN (std::string, item, 2);
  RESULT_COLUMN (int64_t, quantity, 3);
};

} // anonymous namespace

std::map<std::string, Amount>
DexOrderTable::GetReservedCoins (const Database::IdT building) const
{
  std::ostringstream sql;
  sql << R"(
    SELECT `account`, SUM(`quantity` * `price`) AS `cost`
      FROM `dex_orders`
      WHERE `type` = ?1
  )";
  if (building != Database::EMPTY_ID)
    sql << " AND `building` = ?2";
  sql << R"(
      GROUP BY `account`
  )";

  auto stmt = db.Prepare (sql.str ());
  stmt.Bind (1, static_cast<int> (DexOrder::Type::BID));
  if (building != Database::EMPTY_ID)
    stmt.Bind (2, building);

  std::map<std::string, Amount> balances;
  auto res = stmt.Query<ReservedCoinsResult> ();
  while (res.Step ())
    balances.emplace (res.Get<ReservedCoinsResult::account> (),
                      res.Get<ReservedCoinsResult::cost> ());

  return balances;
}

std::map<std::string, Inventory>
DexOrderTable::GetReservedQuantities (const Database::IdT building) const
{
  auto stmt = db.Prepare (R"(
    SELECT `account`, `item`, SUM(`quantity`) AS `quantity`
      FROM `dex_orders`
      WHERE `building` = ?1 AND `type` = ?2
      GROUP BY `account`, `item`
      ORDER BY `account`
  )");
  stmt.Bind (1, building);
  stmt.Bind (2, static_cast<int> (DexOrder::Type::ASK));

  std::map<std::string, Inventory> balances;
  auto res = stmt.Query<ReservedQuantitiesResult> ();

  /* Since we get the results ordered by account, we can keep track of the
     last account's map iterator, and only need to check if that still
     matches the next row.  There is no need to keep looking up the name.  */
  auto lastIt = balances.end ();

  while (res.Step ())
    {
      const auto account = res.Get<ReservedQuantitiesResult::account> ();
      if (lastIt == balances.end () || lastIt->first != account)
        {
          auto ins = balances.emplace (account, Inventory ());
          CHECK (ins.second) << "We already have an entry for " << account;
          lastIt = ins.first;
        }

      lastIt->second.AddFungibleCount (
          res.Get<ReservedQuantitiesResult::item> (),
          res.Get<ReservedQuantitiesResult::quantity> ());
    }

  return balances;
}

void
DexOrderTable::DeleteForBuilding (const Database::IdT building)
{
  auto stmt = db.Prepare (R"(
    DELETE FROM `dex_orders`
      WHERE `building` = ?1
  )");
  stmt.Bind (1, building);
  stmt.Execute ();
}

/* ************************************************************************** */

DexTrade::DexTrade (Database& d)
  : db(d), id(db.GetLogId ()),
    tracker(db.TrackHandle ("dex trade", id)),
    height(0), time(0),
    buildingId(Database::EMPTY_ID),
    quantity(0), price(0),
    isNew(true)
{
  VLOG (1) << "Created new DEX trade entry with ID " << id;
}

DexTrade::DexTrade (Database& d, const Database::Result<DexTradeResult>& res)
  : db(d), isNew(false)
{
  id = res.Get<DexTradeResult::id> ();
  tracker = db.TrackHandle ("dex trade", id);

  height = res.Get<DexTradeResult::height> ();
  time = res.Get<DexTradeResult::time> ();
  buildingId = res.Get<DexTradeResult::building> ();
  item = res.Get<DexTradeResult::item> ();
  quantity = res.Get<DexTradeResult::quantity> ();
  price = res.Get<DexTradeResult::price> ();
  buyer = res.Get<DexTradeResult::buyer> ();
  seller = res.Get<DexTradeResult::seller> ();
}

DexTrade::~DexTrade ()
{
  if (!isNew)
    return;

  VLOG (1) << "Inserting new DEX trade " << id << " into the database";

  CHECK_GT (height, 0) << "No height set for trade entry " << id;
  CHECK_GT (time, 0) << "No timestamp set for trade entry " << id;

  CHECK_NE (buildingId, Database::EMPTY_ID)
      << "No building ID set for trade entry " << id;
  CHECK_NE (item, "")
      << "No item type set for trade entry " << id;

  CHECK_GT (quantity, 0)
      << "No quantity set for trade entry " << id;
  CHECK_LE (quantity, MAX_QUANTITY)
      << "Invalid quantity for trade entry " << id;
  CHECK_GE (price, 0)
      << "Invalid (negative) price for trade entry " << id;

  auto stmt = db.Prepare (R"(
    INSERT INTO `dex_trade_history`
      (`id`, `height`, `time`,
       `building`, `item`,
       `quantity`, `price`,
       `seller`, `buyer`)
      VALUES (?1, ?2, ?3,
              ?4, ?5,
              ?6, ?7,
              ?8, ?9)
  )");

  stmt.Bind (1, id);
  stmt.Bind (2, height);
  stmt.Bind (3, time);
  stmt.Bind (4, buildingId);
  stmt.Bind (5, item);
  stmt.Bind (6, quantity);
  stmt.Bind (7, price);
  stmt.Bind (8, seller);
  stmt.Bind (9, buyer);

  stmt.Execute ();
}

/* ************************************************************************** */

DexHistoryTable::Handle
DexHistoryTable::RecordTrade (const unsigned height, const int64_t time,
                              const Database::IdT building,
                              const std::string& item,
                              const Quantity quantity, const Amount price,
                              const std::string& seller,
                              const std::string& buyer)
{
  Handle o(new DexTrade (db));

  o->height = height;
  o->time = time;
  o->buildingId = building;
  o->item = item;
  o->quantity = quantity;
  o->price = price;
  o->seller = seller;
  o->buyer = buyer;

  return o;
}

DexHistoryTable::Handle
DexHistoryTable::GetFromResult (
    const Database::Result<DexTradeResult>& res) const
{
  return Handle (new DexTrade (db, res));
}

Database::Result<DexTradeResult>
DexHistoryTable::QueryForItem (const std::string& item,
                               const Database::IdT building) const
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `dex_trade_history`
      WHERE `item` = ?1 AND `building` = ?2
      ORDER BY `id`
  )");
  stmt.Bind (1, item);
  stmt.Bind (2, building);
  return stmt.Query<DexTradeResult> ();
}

/* ************************************************************************** */

} // namespace pxd
