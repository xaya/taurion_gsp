#ifndef PXD_JSONUTILS_HPP
#define PXD_JSONUTILS_HPP

#include "amount.hpp"

#include "database/database.hpp"
#include "hexagonal/coord.hpp"

#include <json/json.h>

#include <string>

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
 * Converts an integer value to the proper JSON representation.
 */
template <typename T>
  Json::Value IntToJson (T val);

} // namespace pxd

#endif // PXD_JSONUTILS_HPP
