#ifndef PXD_LOGIC_HPP
#define PXD_LOGIC_HPP

#include "fame.hpp"
#include "params.hpp"

#include "database/database.hpp"
#include "mapdata/basemap.hpp"
#include "proto/character.pb.h"

#include <xayagame/sqlitegame.hpp>
#include <xayautil/random.hpp>

#include <sqlite3.h>

#include <json/json.h>

#include <string>

namespace pxd
{

class SQLiteGameDatabase;

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

};

} // namespace pxd

#endif // PXD_LOGIC_HPP
