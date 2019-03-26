#ifndef PXD_GAMESTATEJSON_HPP
#define PXD_GAMESTATEJSON_HPP

#include "params.hpp"

#include "database/database.hpp"
#include "mapdata/basemap.hpp"

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

  /** Game parameters.  */
  const Params& params;

  /** Basemap instance that can be used.  */
  const BaseMap& map;

  /**
   * Extracts all results from the Database::Result instance, converts them
   * to JSON, and returns a JSON array.
   */
  template <typename T>
    Json::Value ResultsAsArray (T& tbl, Database::Result res) const;

public:

  explicit GameStateJson (Database& d, const Params& p, const BaseMap& m)
    : db(d), params(p), map(m)
  {}

  GameStateJson () = delete;
  GameStateJson (const GameStateJson&) = delete;
  void operator= (const GameStateJson&) = delete;

  /**
   * Converts a state instance (like a Character or Region) to the corresponding
   * JSON value in the game state.
   */
  template <typename T>
    Json::Value Convert (const T& val) const;

  /**
   * Returns the full game state JSON for the given Database handle.  The full
   * game state as JSON should mainly be used for debugging and testing, not
   * in production.  For that, more targeted RPC results should be used.
   */
  Json::Value FullState ();

};

} // namespace pxd

#endif // PXD_GAMESTATEJSON_HPP
