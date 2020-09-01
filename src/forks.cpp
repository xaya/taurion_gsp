/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include <glog/logging.h>

#include <unordered_map>

namespace pxd
{

namespace
{

/** Activation heights by chain for a given fork.  */
using ActivationHeights = std::unordered_map<xaya::Chain, unsigned>;

/** The activation heights for our forks.  */
const std::unordered_map<Fork, ActivationHeights> FORK_HEIGHTS =
  {
    {
      Fork::Dummy,
      {
        {xaya::Chain::MAIN, 3'000'000},
        {xaya::Chain::TEST, 150'000},
        {xaya::Chain::REGTEST, 100},
      },
    },
    {
      Fork::UnblockSpawns,
      {
        {xaya::Chain::MAIN, 2'159'000},
        {xaya::Chain::TEST, 0},
        {xaya::Chain::REGTEST, 500},
      },
    },
  };

} // anonymous namespace

bool
ForkHandler::IsActive (const Fork f) const
{
  const auto mit = FORK_HEIGHTS.find (f);
  CHECK (mit != FORK_HEIGHTS.end ())
      << "Fork height not defined for " << static_cast<int> (f);

  const auto mit2 = mit->second.find (chain);
  CHECK (mit2 != mit->second.end ())
      << "Fork " << static_cast<int> (f)
      << " does not define height for chain " << static_cast<int> (chain);

  return height >= mit2->second;
}

} // namespace pxd
