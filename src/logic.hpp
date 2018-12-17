#ifndef PXD_LOGIC_HPP
#define PXD_LOGIC_HPP

#include <xayagame/sqlitegame.hpp>

#include <sqlite3.h>

#include <json/json.h>

#include <string>

namespace pxd
{

/**
 * The game logic implementation for Project X.  This is the main class that
 * acts as the game-specific code, interacting with libxayagame and the Xaya
 * daemon.  By itself, it is combining the various other classes and functions
 * that implement the real game logic.
 */
class PXLogic : public xaya::SQLiteGame
{

protected:

  void SetupSchema (sqlite3* db) override;

  void GetInitialStateBlock (unsigned& height,
                             std::string& hashHex) const override;
  void InitialiseState (sqlite3* db) override;

  void UpdateState (sqlite3* db, const Json::Value& blockData) override;

  Json::Value GetStateAsJson (sqlite3* db) override;

public:

  /**
   * Construct the game logic instance, given the filename of the SQLite
   * database to open (or create).
   */
  explicit PXLogic (const std::string& f)
    : SQLiteGame(f)
  {}

  PXLogic () = delete;
  PXLogic (const PXLogic&) = delete;
  void operator= (const PXLogic&) = delete;

};

} // namespace pxd

#endif // PXD_LOGIC_HPP
