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

#ifndef PXD_JSONUTILS_HPP
#define PXD_JSONUTILS_HPP

#include "database/amount.hpp"
#include "database/database.hpp"
#include "database/inventory.hpp"
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
 * Parses a Cubit amount from JSON, and verifies that it is roughly in
 * range, i.e. within [0, MAX_COIN_AMOUNT].
 */
bool CoinAmountFromJson (const Json::Value& val, Amount& amount);

/**
 * Parses an item quantity from JSON.  Verifies that it is in the range
 * (0, MAX_QUANTITY].
 */
bool QuantityFromJson (const Json::Value& val, Quantity& quantity);

/**
 * Parses an ID value encoded in JSON.  Returns true if one was found.
 */
bool IdFromJson (const Json::Value& val, Database::IdT& id);

/**
 * Converts an integer value to the proper JSON representation.
 */
template <typename T>
  Json::Value IntToJson (T val);

} // namespace pxd

#endif // PXD_JSONUTILS_HPP
