/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#ifndef PXD_LOGIC_HPP
#define PXD_LOGIC_HPP

#include "fame.hpp"
#include "params.hpp"

#include "database/database.hpp"
#include "mapdata/basemap.hpp"
#include "proto/character.pb.h"

#include <xayagame/sqlitegame.hpp>
#include <xayautil/random.hpp>

#include <json/json.h>

#include <sqlite3.h>

#include <string>

namespace pxd
{

class PXLogic;

/**
 * Database instance that uses an SQLiteGame instance for everything.
 */
class SQLiteGameDatabase : public Database
{

private:

  /** The underlying SQLiteGame instance.  */
  PXLogic& game;

protected:

  sqlite3_stmt* PrepareStatement (const std::string& sql) override;

public:

  explicit SQLiteGameDatabase (PXLogic& g)
    : game(g)
  {}

  SQLiteGameDatabase () = delete;
  SQLiteGameDatabase (const SQLiteGameDatabase&) = delete;
  void operator= (const SQLiteGameDatabase&) = delete;

  Database::IdT GetNextId () override;

};

/**
 * The game logic implementation for Taurion.  This is the main class that
 * acts as the game-specific code, interacting with libxayagame and the Xaya
 * daemon.  By itself, it is combining the various other classes and functions
 * that implement the real game logic.
 */
class PXLogic : public xaya::SQLiteGame
{

private:

  /** The underlying base map data.  */
  const BaseMap map;

  /**
   * Handles the actual logic for the game-state update.  This is extracted
   * here out of UpdateState, so that it can be accessed from unit tests
   * independently of SQLiteGame.
   */
  static void UpdateState (Database& db, xaya::Random& rnd,
                           const Params& params, const BaseMap& map,
                           const Json::Value& blockData);

  /**
   * Updates the state with a custom FameUpdater.  This is used for mocking
   * the instance in tests.
   */
  static void UpdateState (Database& db, FameUpdater& fame, xaya::Random& rnd,
                           const Params& params, const BaseMap& map,
                           const Json::Value& blockData);

  friend class PXLogicTests;
  friend class PXRpcServer;
  friend class SQLiteGameDatabase;

protected:

  void SetupSchema (sqlite3* db) override;

  void GetInitialStateBlock (unsigned& height,
                             std::string& hashHex) const override;
  void InitialiseState (sqlite3* db) override;

  void UpdateState (sqlite3* db, const Json::Value& blockData) override;

  Json::Value GetStateAsJson (sqlite3* db) override;

public:

  PXLogic () = default;

  PXLogic (const PXLogic&) = delete;
  void operator= (const PXLogic&) = delete;

  /**
   * Gives access to the underlying BaseMap instance (so that it can be reused
   * for other parts of the game like pending processing).
   */
  const BaseMap&
  GetBaseMap ()
  {
    return map;
  }

};

} // namespace pxd

#endif // PXD_LOGIC_HPP
