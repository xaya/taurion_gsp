/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2025  Autonomous Worlds Ltd

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

#include "params.hpp"

#include "protoutils.hpp"

#include "proto/roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

bool
Params::IsLowPrizeZone (const HexCoord& pos) const
{
  /* The low-prize zone is in a (large) radius around the starter zones
     and spawn points.  */
  constexpr HexCoord::IntT radius = 1'250;

  const RoConfig cfg(chain);
  for (const auto& sz : cfg->safe_zones ())
    {
      if (!sz.has_faction ())
        continue;

      const auto centre = CoordFromProto (sz.centre ());
      if (HexCoord::DistanceL1 (centre, pos) <= radius)
        return true;
    }

  return false;
}

unsigned
Params::RevEngSuccessChance (const unsigned existingBp) const
{
  constexpr uint64_t fpMultiple = 1'000'000;
  constexpr uint64_t minChance = 1'000'000'000;

  uint64_t base;
  switch (chain)
    {
    case xaya::Chain::MAIN:
    case xaya::Chain::TEST:
      base = 10;
      break;
    case xaya::Chain::REGTEST:
      base = 1;
      break;
    default:
      LOG (FATAL) << "Invalid chain value: " << static_cast<int> (chain);
    }

  /* The base chance is then discounted by a factor of 75% (i.e. the N value
     for 1/N increased accordingly) for each existing blueprint.  The minimum
     chance (preventing mostly integer overflows) is 1/1M.

     At least on regtest with a very low base chance, we have to do the
     calculation in fixed point math (not integer) in order to get
     values above 1.  */

  base *= fpMultiple;
  for (unsigned i = 0; i < existingBp; ++i)
    {
      base = (4 * base) / 3;
      if (base >= fpMultiple * minChance)
        return minChance;
    }
  base /= fpMultiple;

  return base;
}

} // namespace pxd
