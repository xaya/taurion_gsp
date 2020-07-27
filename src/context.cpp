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

#include "context.hpp"

#include <glog/logging.h>

namespace pxd
{

Context::Context (const xaya::Chain c)
  : map(nullptr), chain(c),
    height(0), timestamp(NO_TIMESTAMP)
{}

Context::Context (const xaya::Chain c, const BaseMap& m,
                  const unsigned h, const int64_t ts)
  : map(&m), chain(c),
    params(new pxd::Params (chain)),
    cfg(new pxd::RoConfig (chain)),
    height(h), timestamp(ts)
{}

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

} // namespace pxd
