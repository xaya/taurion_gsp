/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020-2025  Autonomous Worlds Ltd

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

#include "forks.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <unordered_map>

namespace pxd
{

/* The flags are not in an anonymous namespace, as we will need to DECLARE
   and access (modify) them for unit tests.  */
DEFINE_int32 (fork_height_gamestart, -1,
              "if set, override the fork height for \"game start\"");

namespace
{

/**
 * Data specification for one particular fork.
 */
struct ForkData
{

  /** The activation heights by chain.  */
  std::unordered_map<xaya::Chain, unsigned> heights;

  /**
   * If set, the flag variable that will override the activation height
   * (if that flag is set).
   */
  const int* overrideFlag;

};

/** The activation heights for our forks.  */
const std::unordered_map<Fork, ForkData> FORK_HEIGHTS =
  {
    {
      Fork::Dummy,
      {
        {
          {xaya::Chain::MAIN, 3'000'000},
          {xaya::Chain::REGTEST, 100},
        },
        nullptr,
      }
    },
    {
      Fork::GameStart,
      {
        {
          {xaya::Chain::MAIN, 80'528'098},
          {xaya::Chain::REGTEST, 0},
        },
        &FLAGS_fork_height_gamestart,
      }
    },
  };

} // anonymous namespace

xaya::Chain
ForkHandler::TranslateChain (const xaya::Chain c)
{
  switch (c)
    {
    case xaya::Chain::MAIN:
    case xaya::Chain::POLYGON:
      return xaya::Chain::MAIN;

    case xaya::Chain::TEST:
    case xaya::Chain::MUMBAI:
      return xaya::Chain::TEST;

    case xaya::Chain::REGTEST:
    case xaya::Chain::GANACHE:
      return xaya::Chain::REGTEST;

    default:
      LOG (FATAL) << "Unexpected chain: " << xaya::ChainToString (c);
    }
}

bool
ForkHandler::IsActive (const Fork f) const
{
  const auto mit = FORK_HEIGHTS.find (f);
  CHECK (mit != FORK_HEIGHTS.end ())
      << "Fork height not defined for " << static_cast<int> (f);
  const auto& data = mit->second;

  if (data.overrideFlag != nullptr && *data.overrideFlag >= 0)
    return static_cast<int> (height) >= *data.overrideFlag;

  const auto mit2 = data.heights.find (chain);
  CHECK (mit2 != data.heights.end ())
      << "Fork " << static_cast<int> (f)
      << " does not define height for chain " << static_cast<int> (chain);
  return height >= mit2->second;
}

} // namespace pxd
