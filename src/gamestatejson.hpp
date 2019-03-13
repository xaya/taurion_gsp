#ifndef PXD_GAMESTATEJSON_HPP
#define PXD_GAMESTATEJSON_HPP

#include "database/database.hpp"

#include <json/json.h>

namespace pxd
{

/**
 * Converts a state instance (like a Character or Region) to the corresponding
 * JSON value in the game state.
 */
template <typename T>
  Json::Value ToStateJson (const T& val);

/**
 * Returns the full game state JSON for the given Database handle.  The full
 * game state as JSON should mainly be used for debugging and testing, not
 * in production.  For that, more targeted RPC results should be used.
 */
Json::Value GameStateToJson (Database& db);

} // namespace pxd

#endif // PXD_GAMESTATEJSON_HPP
