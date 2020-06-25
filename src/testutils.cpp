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

#include "testutils.hpp"

#include <xayautil/hash.hpp>

#include <glog/logging.h>

#include <sstream>

namespace pxd
{

TestRandom::TestRandom ()
{
  xaya::SHA256 seed;
  seed << "test seed";
  Seed (seed.Finalise ());
}

void
ContextForTesting::SetChain (const xaya::Chain c)
{
  LOG (INFO) << "Setting context chain to " << xaya::ChainToString (c);
  chain = c;
  mapInstance = std::make_unique<BaseMap> (chain);
  map = mapInstance.get ();
  params = std::make_unique<pxd::Params> (chain);
  cfg = std::make_unique<pxd::RoConfig> (chain);
}

void
ContextForTesting::SetHeight (const unsigned h)
{
  LOG (INFO) << "Setting context height to " << h;
  height = h;
}

void
ContextForTesting::SetTimestamp (const int64_t ts)
{
  LOG (INFO) << "Setting context timestamp to " << ts;
  timestamp = ts;
}

Json::Value
ParseJson (const std::string& str)
{
  Json::Value val;
  std::istringstream in(str);
  in >> val;
  return val;
}

bool
PartialJsonEqual (const Json::Value& actual, const Json::Value& expected)
{
  if (!expected.isObject () && !expected.isArray ())
    {
      /* If the expected value is the literal string "null", then we compare
         it equal to a null value.  This allows us to test for explicit
         null's even if we use null values as placeholder for "field should
         be missing".  */
      if (expected.isString () && expected.asString () == "null")
        return actual.isNull ();

      /* Special case:  If both values are integers, then we compare them
         explicitly here.  This allows values of type "unsigned int" to be
         equal to values of type "int" (from golden data).  */
      if (actual.isInt64 () && expected.isInt64 ()
            && actual.asInt64 () == expected.asInt64 ())
        return true;

      if (actual == expected)
        return true;

      LOG (ERROR)
          << "Actual value:\n" << actual
          << "\nis not equal to expected:\n" << expected;
      return false;
    }

  if (expected.isArray ())
    {
      if (!actual.isArray ())
        {
          LOG (ERROR) << "Expected value is array, actual not: " << actual;
          return false;
        }

      if (actual.size () != expected.size ())
        {
          LOG (ERROR)
              << "Array sizes do not match: got " << actual.size ()
              << ", want " << expected.size ();
          return false;
        }

      for (unsigned i = 0; i < expected.size (); ++i)
        if (!PartialJsonEqual (actual[i], expected[i]))
          return false;

      return true;
    }

  if (!actual.isObject ())
    {
      LOG (ERROR) << "Expected value is object, actual not: " << actual;
      return false;
    }

  for (const auto& expectedKey : expected.getMemberNames ())
    {
      const auto& expectedVal = expected[expectedKey];
      if (expectedVal.isNull ())
        {
          if (actual.isMember (expectedKey))
            {
              LOG (ERROR)
                  << "Actual has member expected to be not there: "
                  << expectedKey;
              return false;
            }
          continue;
        }

      if (!actual.isMember (expectedKey))
        {
          LOG (ERROR)
              << "Actual does not have expected member: " << expectedKey;
          return false;
        }

      if (!PartialJsonEqual (actual[expectedKey], expected[expectedKey]))
        return false;
    }

  return true;
}

} // namespace pxd
