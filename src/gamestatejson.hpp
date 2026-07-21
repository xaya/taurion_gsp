/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2026  Autonomous Worlds Ltd

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

#ifndef PXD_GAMESTATEJSON_HPP
#define PXD_GAMESTATEJSON_HPP

#include "context.hpp"

#include "database/damagelists.hpp"
#include "database/database.hpp"
#include "database/dex.hpp"
#include "database/inventory.hpp"
#include "mapdata/basemap.hpp"
#include "proto/building.pb.h"

#include <json/json.h>

namespace pxd
{

/**
 * Utility class that handles construction of game-state JSON.
 */
class GameStateJson
{

private:

  /** Database to read from.  */
  Database& db;

  /**
   * Database table to access building inventories.  This needs to be a
   * member field so that the "convert" function for buildings can access
   * it without needing any more arguments.
   */
  mutable BuildingInventoriesTable buildingInventories;

  /** Damage lists accessor (for adding the attackers to a character JSON).  */
  const DamageLists dl;

  /**
   * Database table for DEX orders.  This is also used from the "convert"
   * function for buildings.
   */
  mutable DexOrderTable orders;

  /** Current parameter context.  */
  const Context& ctx;

  /**
   * Extracts all results from the Database::Result instance, converts them
   * to JSON, and returns a JSON array.
   */
  template <typename T, typename R>
    Json::Value ResultsAsArray (T& tbl, Database::Result<R> res) const;

  /**
   * The job fields shared between a live board row (Job) and a settled
   * history row (JobHistoryEntry) -- everything except the live status /
   * the settlement metadata, which the two Convert specialisations add.
   */
  template <typename J>
    Json::Value JobCommonJson (const J& j) const;

public:

  explicit GameStateJson (Database& d, const Context& c)
    : db(d), buildingInventories(db), dl(db), orders(db), ctx(c)
  {}

  GameStateJson () = delete;
  GameStateJson (const GameStateJson&) = delete;
  void operator= (const GameStateJson&) = delete;

  /**
   * Converts a building configuration proto to JSON.  This does not need
   * any members and can thus be static (and is being used as such from
   * the pending code).
   */
  static Json::Value Convert (const proto::Building::Config& val);

  /**
   * Converts a state instance (like a Character or Region) to the corresponding
   * JSON value in the game state.
   */
  template <typename T>
    Json::Value Convert (const T& val) const;

  /**
   * Returns the JSON data representing all accounts in the game state.
   */
  Json::Value Accounts ();

  /**
   * Returns the JSON data representing all buildings in the game state.
   */
  Json::Value Buildings ();

  /**
   * Returns the JSON data representing all jobs on the jobs board.  Only
   * reachable through the full game-state export (like every other table);
   * there is no standalone whole-board RPC -- per-table reads go through
   * the hard-capped JobsPage.
   */
  Json::Value Jobs ();

  /**
   * Returns one hard-capped page of the jobs board, ordered by ID.
   * `afterId` is an exclusive continuation cursor (0 = from the start).
   */
  Json::Value JobsPage (Database::IdT afterId, int limit);

  /**
   * Returns the JSON data for the settled-jobs history (the job_history
   * table), from the given settlement timestamp onwards (0 = the whole
   * retention window).
   */
  Json::Value JobsHistory (int64_t fromTime, int64_t afterTime,
                           int64_t afterId, int limit);

  /**
   * Returns the JSON data representing all characters in the game state.
   */
  Json::Value Characters ();

  /**
   * Returns the JSON data representing all ground loot.
   */
  Json::Value GroundLoot ();

  /**
   * Returns the JSON data about all ongoing operations.
   */
  Json::Value OngoingOperations ();

  /**
   * Returns the JSON data representing all regions in the game state which
   * where modified after the given block height.
   */
  Json::Value Regions (unsigned h);

  /**
   * Returns the JSON data about money supply and burnsale stats.
   */
  Json::Value MoneySupply ();

  /**
   * Returns the JSON data representing the available and found prizes
   * for prospecting.
   */
  Json::Value PrizeStats ();

  /**
   * Returns the trade history for a given item and building.
   */
  Json::Value TradeHistory (const std::string& item, Database::IdT building);

  /**
   * Returns data about the current superblock state.
   */
  Json::Value SuperBlock ();

  /**
   * Returns the full game state JSON for the given Database handle.  The full
   * game state as JSON should mainly be used for debugging and testing, not
   * in production.  For that, more targeted RPC results should be used.
   */
  Json::Value FullState ();

  /**
   * Returns the bootstrap data that the frontend needs on startup (e.g.
   * including all regions, not just recently-modified ones).  This is
   * potentially an expensive operation and has a large result.
   */
  Json::Value BootstrapData ();

};

} // namespace pxd

#endif // PXD_GAMESTATEJSON_HPP
