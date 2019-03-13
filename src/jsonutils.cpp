#include "jsonutils.hpp"

#include <glog/logging.h>

#include <cmath>
#include <limits>
#include <sstream>

namespace pxd
{

namespace
{

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
      LOG (ERROR)
          << "Invalid HexCoord: JSON value " << val << " is not an object";
      return false;
    }

  const Json::Value* xMember = val.find (COORD_X, COORD_X + 1);
  const Json::Value* yMember = val.find (COORD_Y, COORD_Y + 1);

  if (xMember == nullptr || yMember == nullptr)
    {
      LOG (ERROR)
          << "Invalid HexCoord: JSON value " << val
          << " must have 'x' and 'y' members";
      return false;
    }

  if (val.size () != 2)
    {
      LOG (ERROR)
          << "Invalid HexCoord: JSON value " << val
          << " has extra members";
      return false;
    }

  if (!xMember->isInt64 () || !yMember->isInt64 ())
    {
      LOG (ERROR)
          << "Invalid HexCoord: JSON value " << val
          << " has non-int64 coordinates";
      return false;
    }

  const int64_t x = xMember->asInt64 ();
  const int64_t y = yMember->asInt64 ();

  using intLimits = std::numeric_limits<HexCoord::IntT>;
  if (x < intLimits::min () || x > intLimits::max ())
    {
      LOG (ERROR)
          << "Invalid HexCoord: x coordinate " << x << " is out of range";
      return false;
    }
  if (y < intLimits::min () || y > intLimits::max ())
    {
      LOG (ERROR)
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

  amount = std::lround (dval);
  return true;
}

bool
IdFromString (const std::string& str, Database::IdT& id)
{
  std::istringstream in(str);
  in >> id;

  if (id == Database::EMPTY_ID)
    return false;

  std::ostringstream out;
  out << id;

  return out.str () == str;
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
