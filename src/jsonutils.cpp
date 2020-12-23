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

#include "jsonutils.hpp"

#include <xayautil/jsonutils.hpp>

#include <glog/logging.h>

#include <limits>

namespace pxd
{

namespace
{

/**
 * The maximum amount of vCHI in a move.  This is consensus relevant.
 * The value here is actually the total cap on vCHI (although that's not
 * relevant in this context).
 */
constexpr Amount MAX_COIN_AMOUNT = 100'000'000'000;

constexpr Database::IdT MAX_ID = 999999999;

constexpr const char COORD_X[] = "x";
constexpr const char COORD_Y[] = "y";

/* The expected string length is hardcoded as one below, make sure that isn't
   broken accidentally.  */
static_assert (sizeof (COORD_X) == 2, "COORD_X must be string of length 1");
static_assert (sizeof (COORD_Y) == 2, "COORD_X must be string of length 1");

} // anonymous namespace

Json::Value
CoordToJson (const HexCoord& c)
{
  Json::Value res(Json::objectValue);
  res[COORD_X] = c.GetX ();
  res[COORD_Y] = c.GetY ();

  return res;
}

bool
CoordFromJson (const Json::Value& val, HexCoord& c)
{
  if (!val.isObject ())
    {
      VLOG (1)
          << "Invalid HexCoord: JSON value " << val << " is not an object";
      return false;
    }

  const Json::Value* xMember = val.find (COORD_X, COORD_X + 1);
  const Json::Value* yMember = val.find (COORD_Y, COORD_Y + 1);

  if (xMember == nullptr || yMember == nullptr)
    {
      VLOG (1)
          << "Invalid HexCoord: JSON value " << val
          << " must have 'x' and 'y' members";
      return false;
    }

  if (val.size () != 2)
    {
      VLOG (1)
          << "Invalid HexCoord: JSON value " << val
          << " has extra members";
      return false;
    }

  if (!xMember->isInt64 () || !xaya::IsIntegerValue (*xMember)
        || !yMember->isInt64 () || !xaya::IsIntegerValue (*yMember))
    {
      VLOG (1)
          << "Invalid HexCoord: JSON value " << val
          << " has non-int64 coordinates";
      return false;
    }

  const int64_t x = xMember->asInt64 ();
  const int64_t y = yMember->asInt64 ();

  using intLimits = std::numeric_limits<HexCoord::IntT>;
  if (x < intLimits::min () || x > intLimits::max ())
    {
      VLOG (1)
          << "Invalid HexCoord: x coordinate " << x << " is out of range";
      return false;
    }
  if (y < intLimits::min () || y > intLimits::max ())
    {
      VLOG (1)
          << "Invalid HexCoord: y coordinate " << y << " is out of range";
      return false;
    }

  c = HexCoord (x, y);
  return true;
}

bool
CoinAmountFromJson (const Json::Value& val, Amount& amount)
{
  if (!val.isInt64 () || !xaya::IsIntegerValue (val))
    return false;

  amount = val.asInt64 ();
  return amount >= 0 && amount <= MAX_COIN_AMOUNT;
}

bool
QuantityFromJson (const Json::Value& val, Quantity& quantity)
{
  if (!val.isInt64 () || !xaya::IsIntegerValue (val))
    return false;

  quantity = val.asInt64 ();
  return quantity > 0 && quantity <= MAX_QUANTITY;
}

bool
IdFromJson (const Json::Value& val, Database::IdT& id)
{
  if (!val.isUInt64 () || !xaya::IsIntegerValue (val))
    return false;

  id = val.asUInt64 ();
  return id > 0 && id < MAX_ID;
}

template <>
  Json::Value
  IntToJson<int32_t> (const int32_t val)
{
  return static_cast<Json::Int> (val);
}

template <>
  Json::Value
  IntToJson<uint32_t> (const uint32_t val)
{
  return static_cast<Json::UInt> (val);
}

template <>
  Json::Value
  IntToJson<int64_t> (const int64_t val)
{
  return static_cast<Json::Int64> (val);
}

template <>
  Json::Value
  IntToJson<uint64_t> (const uint64_t val)
{
  return static_cast<Json::UInt64> (val);
}

} // namespace pxd
