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

#include <glog/logging.h>

#include <cmath>
#include <limits>

namespace pxd
{

namespace
{

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

  if (!xMember->isInt64 () || !yMember->isInt64 ())
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

Json::Value
AmountToJson (const Amount amount)
{
  return Json::Value (static_cast<double> (amount) / COIN);
}

bool
AmountFromJson (const Json::Value& val, Amount& amount)
{
  if (!val.isDouble ())
    {
      LOG (ERROR) << "JSON value for amount is not double: " << val;
      return false;
    }
  const double dval = val.asDouble () * COIN;

  if (dval < 0.0 || dval > MAX_AMOUNT)
    {
      LOG (ERROR) << "Amount " << (dval / COIN) << " is out of range";
      return false;
    }

  amount = std::llround (dval);
  VLOG (1) << "Converted JSON " << val << " to amount: " << amount;

  /* Sanity check once more, to guard against potential overflow bugs.  */
  CHECK_GE (amount, 0);
  CHECK_LE (amount, MAX_AMOUNT);

  return true;
}

bool
IdFromJson (const Json::Value& val, Database::IdT& id)
{
  if (!val.isUInt64 ())
    return false;

  id = val.asUInt64 ();
  return id > 0 && id < MAX_ID;
}

bool
IdFromString (const std::string& str, Database::IdT& id)
{
  std::vector<Database::IdT> ids;
  if (!IdArrayFromString (str, ids))
    return false;

  if (ids.size () != 1)
    return false;

  id = ids[0];
  return true;
}

bool
IdArrayFromString (const std::string& str, std::vector<Database::IdT>& ids)
{
  ids.clear ();

  bool inNumber = false;
  Database::IdT num;
  for (const char c : str)
    {
      if (c == ',')
        {
          if (!inNumber)
            return false;
          ids.push_back (num);
          inNumber = false;
          continue;
        }

      if (c < '0' || c > '9')
        return false;

      const Database::IdT digit = c - '0';

      if (!inNumber)
        {
          /* Zero is not a valid ID.  Thus there should never be zeros
             at the beginning of a number.  */
          if (digit == 0)
            return false;

          inNumber = true;
          num = digit;
          continue;
        }

      /* Very simple check for out-of-bounds.  In practice, we will never
         see such large IDs anyway.  Nevertheless this is consensus-critical,
         as it may influence how we handle an entire command where only some
         IDs are out-of-range (ignore the entire command because it is parsed
         invalid vs just ignore the individual IDs because they do not
         match an existing entity in the database).  */

      CHECK_GT (num, 0);
      num = 10 * num + digit;

      if (num > MAX_ID)
        return false;
    }

  /* At the end, we should have a current number unless the string was
     simply empty.  In that case, we need to push it.  If there is no
     current number, it likely means that we ended with a comma (which
     is invalid).  */
  if (inNumber)
    ids.push_back (num);
  else if (!ids.empty ())
    return false;

  return true;
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
