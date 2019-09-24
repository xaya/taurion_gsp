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

#ifndef PXD_JSONUTILS_HPP
#define PXD_JSONUTILS_HPP

#include "amount.hpp"

#include "database/database.hpp"
#include "hexagonal/coord.hpp"

#include <json/json.h>

#include <string>
#include <vector>

namespace pxd
{

/**
 * Encodes a HexCoord object into a JSON object, so that it can be returned
 * from the JSON-RPC interface.
 *
 * The format is: {"x": x-coord, "y": y-coord}
 */
Json::Value CoordToJson (const HexCoord& c);

/**
 * Parses a JSON object (e.g. passed by RPC) into a HexCoord.  Returns false
 * if the format isn't right, e.g. the values are out of range for
 * HexCoord::IntT or the object is missing keys.
 */
bool CoordFromJson (const Json::Value& val, HexCoord& c);

/**
 * Encodes a CHI amount into a JSON value.
 */
Json::Value AmountToJson (Amount amount);

/**
 * Parses a JSON value into a CHI amount.  Returns false if the value is
 * not valid.
 */
bool AmountFromJson (const Json::Value& val, Amount& amount);

/**
 * Parses an ID value encoded as a string (e.g. for a dictionary key in JSON).
 * Returns true if the string represents exactly a valid unsigned integer and
 * false if something is wrong.
 */
bool IdFromString (const std::string& str, Database::IdT& id);

/**
 * Parses a string that encodes one or more IDs (separated by commas), e.g.
 * from a character-update JSON command.  Returns true and fills in the vector
 * of parsed IDs on success, and returns false if the string is not valid.
 *
 * The empty string is valid, and corresponds to an empty array that will be
 * returned for it.  A string like " " or " 1 " is invalid.
 */
bool IdArrayFromString (const std::string& str,
                        std::vector<Database::IdT>& ids);

/**
 * Converts an integer value to the proper JSON representation.
 */
template <typename T>
  Json::Value IntToJson (T val);

} // namespace pxd

#endif // PXD_JSONUTILS_HPP
