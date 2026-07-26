/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2026  Autonomous Worlds Ltd

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

#include "context.hpp"

#include <glog/logging.h>

namespace pxd
{

Context::Context (const xaya::Chain c)
  : map(nullptr), chain(c),
    height(0), blockHeight(0),
    timestamp(NO_TIMESTAMP)
{}

Context::Context (const xaya::Chain c, const BaseMap& m,
                  const unsigned h, const unsigned bh, const int64_t ts)
  : map(&m), chain(c),
    height(h), blockHeight(bh), timestamp(ts)
{
  RefreshInstances ();
}

void
Context::RefreshInstances ()
{
  params = std::make_unique<pxd::Params> (chain);
  cfg = std::make_unique<pxd::RoConfig> (chain);
  /* Fork activation heights are REAL chain-block heights (GameStart is a Polygon
     block number in the tens of millions), so forks must be evaluated against the
     actual block height, NOT the super-block height -- that one counts
     super-blocks from genesis and would never reach a chain-height threshold, so
     every fork would stay inactive forever and each gameplay move would be
     silently dropped after creating only the bare account.  The distinction is
     invisible to the REGTEST unit tests, where every fork height is 0.  */
  forks = std::make_unique<ForkHandler> (chain, blockHeight);
}

unsigned
Context::Height () const
{
  CHECK_NE (height, NO_HEIGHT);
  return height;
}

int64_t
Context::Timestamp () const
{
  CHECK_NE (timestamp, NO_TIMESTAMP);
  return timestamp;
}

unsigned
Context::BlockHeight () const
{
  CHECK_NE (blockHeight, NO_HEIGHT);
  return blockHeight;
}

} // namespace pxd
