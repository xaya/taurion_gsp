/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

  ANCIENT = 4,

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
 * A database result that includes a "faction" column.
 */
struct ResultWithFaction : public Database::ResultType
{
  RESULT_COLUMN (int64_t, faction, 50);
};

/**
 * Retrieves a faction from a database column.  This function verifies that
 * the database value represents a valid faction.  Otherwise it crashes the
 * process (data corruption).
 *
 * This is templated, so that it can accept different database result types.
 * They should all be derived from ResultWithFaction, though.
 */
template <typename T>
  Faction GetFactionFromColumn (const Database::Result<T>& res);

/**
 * Retrieves a faction from a database column, which can also be NULL.
 * In the case of NULL, Faction::INVALID is returned.  Any other
 * value (i.e. non-matching integer values) will CHECK-fail.
 */
template <typename T>
  Faction GetNullableFactionFromColumn (const Database::Result<T>& res);

/**
 * Binds a faction value to a statement parameter.  If f is Faction::INVALID,
 * then a NULL will be bound instead.
 */
void BindFactionParameter (Database::Statement& stmt, unsigned ind, Faction f);

} // namespace pxd

#include "faction.tpp"

#endif // DATABASE_FACTION_HPP
