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

#ifndef DATABASE_DEX_HPP
#define DATABASE_DEX_HPP

#include "amount.hpp"
#include "database.hpp"
#include "inventory.hpp"

#include <memory>
#include <string>

namespace pxd
{

/* ************************************************************************** */

/**
 * Database result type for rows from the dex-orders table.
 */
struct DexOrderResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (int64_t, building, 2);
  RESULT_COLUMN (std::string, account, 3);
  RESULT_COLUMN (int64_t, type, 4);
  RESULT_COLUMN (std::string, item, 5);
  RESULT_COLUMN (int64_t, quantity, 6);
  RESULT_COLUMN (int64_t, price, 7);
};

/**
 * Wrapper class around a DEX order in the database.  Instances should be
 * obtained through DexOrderTable.  Once created, instances are "mostly"
 * immutable; only the order amount can be changed, which is what we need
 * during partial order fill.
 */
class DexOrder
{

public:

  /**
   * Type of an order.  The numeric values match the values stored
   * in the database integer field.
   */
  enum class Type
  {
    INVALID = 0,
    BID = 1,
    ASK = 2,
  };

private:

  /** Database reference this belongs to.  */
  Database& db;

  /** The underlying ID in the database.  */
  Database::IdT id;

  /** The building this is in.  */
  Database::IdT buildingId;

  /** The account this belongs to.  */
  std::string account;

  /** Type of this order.  */
  Type type;

  /** The type of item.  */
  std::string item;

  /** The quantity of the item.  */
  Quantity quantity;

  /** The price in Cubits of one unit.  */
  Amount price;

  /** Whether or not this is a new instance.  */
  bool isNew;

  /** Whether or not this is an existing but dirty instance.  */
  bool dirty;

  /**
   * Constructs a new instance with auto-generated ID meant to be inserted
   * into the database.
   */
  explicit DexOrder (Database& d);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by a DexOrderTable.
   */
  explicit DexOrder (Database& d,
                     const Database::Result<DexOrderResult>& res);

  friend class DexOrderTable;

public:

  /**
   * In the destructor, the underlying database is updated if there are any
   * modifications to send.
   */
  ~DexOrder ();

  DexOrder () = delete;
  DexOrder (const DexOrder&) = delete;
  void operator= (const DexOrder&) = delete;

  Database::IdT
  GetId () const
  {
    return id;
  }

  Database::IdT
  GetBuilding () const
  {
    return buildingId;
  }

  const std::string&
  GetAccount () const
  {
    return account;
  }

  Type
  GetType () const
  {
    return type;
  }

  const std::string&
  GetItem () const
  {
    return item;
  }

  Quantity
  GetQuantity () const
  {
    return quantity;
  }

  Amount
  GetPrice () const
  {
    return price;
  }

  /**
   * Updates the quantity by subtracting the given amount from it.  If this
   * brings the quantity to zero, the order will be deleted from the DB.
   */
  void ReduceQuantity (Quantity q);

  /**
   * Marks this row to be deleted (which effectively means reducing the
   * quantity to zero).
   */
  void Delete ();

};

/**
 * Utility class that handles querying the table of DEX orders with the things
 * needed, and also handles the creation of DexOrder instances.
 */
class DexOrderTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to an instance.  */
  using Handle = std::unique_ptr<DexOrder>;

  explicit DexOrderTable (Database& d)
    : db(d)
  {}

  DexOrderTable () = delete;
  DexOrderTable (const DexOrderTable&) = delete;
  void operator= (const DexOrderTable&) = delete;

  /**
   * Inserts a new entry into the database and returns a handle to it.
   */
  Handle CreateNew (Database::IdT building, const std::string& account,
                    DexOrder::Type type, const std::string& item,
                    Quantity quantity, Amount price);

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<DexOrderResult>& res);

  /**
   * Returns a handle for the given ID (or null if it doesn't exist).
   */
  Handle GetById (Database::IdT id);

  /**
   * Queries the database for all orders in the entire game world.
   */
  Database::Result<DexOrderResult> QueryAll ();

  /**
   * Queries the database for all orders inside the given building.  The
   * results are returned in a way that allows direct building up of
   * order books.  For any (item, type) pair, the matching results
   * will be sorted increasing by price.
   */
  Database::Result<DexOrderResult> QueryForBuilding (Database::IdT building);

  /**
   * Queries the database for all sell orders of a given building and item,
   * where prices are not higher than the limit.  They will be returned sorted
   * by increasing price (and ID as tie breaker).  This is the query one
   * needs for matching a new bid.
   */
  Database::Result<DexOrderResult> QueryToMatchBid (
      Database::IdT building, const std::string& item, Amount limitPrice);

  /**
   * Queries for all buy orders, similar to QueryToMatchBid.  The
   * results are returned ordered by decreasing price until the limit.
   * This is what one needs to match a new ask.
   */
  Database::Result<DexOrderResult> QueryToMatchAsk (
      Database::IdT building, const std::string& item, Amount limitPrice);

  /**
   * Returns the reserved Cubits per account inside the given building
   * or the entire game world if building is EMPTY_ID.
   */
  std::map<std::string, Amount> GetReservedCoins (
      Database::IdT building = Database::EMPTY_ID) const;

  /**
   * Returns the reserved item quantities (from open bids) of all accounts
   * inside a given building.
   */
  std::map<std::string, Inventory> GetReservedQuantities (
      Database::IdT building) const;

  /**
   * Deletes all orders of a given building.
   */
  void DeleteForBuilding (Database::IdT building);

};

/* ************************************************************************** */

/**
 * Database result type for rows from the trade-history table.
 */
struct DexTradeResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, height, 1);
  RESULT_COLUMN (int64_t, time, 2);
  RESULT_COLUMN (int64_t, building, 3);
  RESULT_COLUMN (std::string, item, 4);
  RESULT_COLUMN (int64_t, quantity, 5);
  RESULT_COLUMN (int64_t, price, 6);
  RESULT_COLUMN (std::string, seller, 7);
  RESULT_COLUMN (std::string, buyer, 8);
};

/**
 * Wrapper class around a DEX trade history result.  Rows can be created
 * through DexHistoryTable and are then immutable.  They can also be queried
 * from there and read.
 */
class DexTrade
{

private:

  /** Database reference this belongs to.  */
  Database& db;

  /** The ID for this, if inserting a new entry.  */
  Database::IdT id;

  /** The block height of the trade.  */
  unsigned height;

  /** The timestamp of the trade.  */
  int64_t time;

  /** The building this is in.  */
  Database::IdT buildingId;

  /** The type of item.  */
  std::string item;

  /** The quantity of the item.  */
  Quantity quantity;

  /** The price in Cubits of one unit.  */
  Amount price;

  /** The seller account.  */
  std::string seller;

  /** The buyer account.  */
  std::string buyer;

  /** Whether or not this is a new instance.  */
  bool isNew;

  /**
   * Constructs a new instance with auto-generated ID meant to be inserted
   * into the database.
   */
  explicit DexTrade (Database& d);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by a DexHistoryTable.  This instance
   * will be immutable.
   */
  explicit DexTrade (Database& d,
                     const Database::Result<DexTradeResult>& res);

  friend class DexHistoryTable;

public:

  /**
   * In the destructor, the underlying database is updated if this is a new
   * entry to be inserted.
   */
  ~DexTrade ();

  DexTrade () = delete;
  DexTrade (const DexTrade&) = delete;
  void operator= (const DexTrade&) = delete;

  unsigned
  GetHeight () const
  {
    return height;
  }

  int64_t
  GetTimestamp () const
  {
    return time;
  }

  Database::IdT
  GetBuilding () const
  {
    return buildingId;
  }

  const std::string&
  GetItem () const
  {
    return item;
  }

  Quantity
  GetQuantity () const
  {
    return quantity;
  }

  Amount
  GetPrice () const
  {
    return price;
  }

  const std::string&
  GetSeller () const
  {
    return seller;
  }

  const std::string&
  GetBuyer () const
  {
    return buyer;
  }

};

/**
 * Utility class that handles querying the table of DEX trade history with the
 * things needed, and also handles the creation of DexTrade instances.
 */
class DexHistoryTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to an instance.  */
  using Handle = std::unique_ptr<DexTrade>;

  explicit DexHistoryTable (Database& d)
    : db(d)
  {}

  DexHistoryTable () = delete;
  DexHistoryTable (const DexHistoryTable&) = delete;
  void operator= (const DexHistoryTable&) = delete;

  /**
   * Inserts a new entry into the database and returns a handle to it.
   */
  Handle RecordTrade (unsigned height, int64_t time,
                      Database::IdT building, const std::string& item,
                      Quantity quantity, Amount price,
                      const std::string& seller, const std::string& buyer);

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<DexTradeResult>& res) const;

  /**
   * Queries the database for the trade history of a particular item
   * in a particular building.  Results are returned by increasing ID
   * (corresponding from old to new).
   */
  Database::Result<DexTradeResult> QueryForItem (const std::string& item,
                                                 Database::IdT building) const;

};

/* ************************************************************************** */

} // namespace pxd

#endif // DATABASE_DEX_HPP
