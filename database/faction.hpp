#ifndef DATABASE_FACTION_HPP
#define DATABASE_FACTION_HPP

#include "database.hpp"

#include <cstdint>
#include <string>

namespace pxd
{

/**
 * A faction in the game (as attribute of a user or building).
 */
enum class Faction : int8_t
{

  /* The enum "names" used here are codenames, not the real ones from the
     game (as seen by actual end users).  The numbers are important, as they
     map to database entries.  */

  INVALID = 0,

  RED = 1,
  GREEN = 2,
  BLUE = 3,

};

/**
 * Converts the faction to a string.  This is used for logging and other
 * messages, as well as in the JSON format of game states.
 */
std::string FactionToString (Faction f);

/**
 * Parses a faction value from a string.  Returns INVALID if the string does
 * not represent any of the real factions.
 */
Faction FactionFromString (const std::string& str);

/**
 * Retrieves a faction from a database column.  This function verifies that
 * the database value represents a valid faction.  Otherwise it crashes the
 * process (data corruption).
 */
Faction GetFactionFromColumn (const Database::Result& res,
                              const std::string& col);

/**
 * Binds a faction value to a statement parameter.
 */
void BindFactionParameter (Database::Statement& stmt, unsigned ind, Faction f);

} // namespace pxd

#endif // DATABASE_FACTION_HPP
