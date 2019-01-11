#ifndef PXD_GAMESTATEJSON_HPP
#define PXD_GAMESTATEJSON_HPP

#include "database/character.hpp"
#include "database/database.hpp"

#include <json/json.h>

namespace pxd
{

/**
 * Converts a Character instance to its JSON form in the game-state RPCs.
 */
Json::Value CharacterToJson (const Character& c);

/**
 * Returns the full game state JSON for the given Database handle.  The full
 * game state as JSON should mainly be used for debugging and testing, not
 * in production.  For that, more targeted RPC results should be used.
 */
Json::Value GameStateToJson (Database& db);

} // namespace pxd

#endif // PXD_GAMESTATEJSON_HPP
