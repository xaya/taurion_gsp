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

#include "spawn.hpp"

#include "fitments.hpp"
#include "protoutils.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/ring.hpp"

#include <glog/logging.h>

namespace pxd
{

namespace
{

/**
 * Picks a random location within the given L1 distance of the centre.
 * This is the first part of choosing a spawning location.
 */
HexCoord
RandomSpawnLocation (const HexCoord& centre, const HexCoord::IntT radius,
                     xaya::Random& rnd)
{
  /* The idea is simple:  Choose a random coordinate with x and y within
     radius of centre.x/y.  Those points are guaranteed to include all
     valid points (within L1 distance), although there may be some that
     actually are not in L1 distance.  If we hit one of them, just
     retry.  Approximating the hexagonal L1 range with a circle, the
     probability of succeeding on a try is pi/4.  That seems good enough.  */

  VLOG (2)
      << "Picking random location in L1 radius " << radius
      << " around " << centre << "...";

  while (true)
    {
      HexCoord res = centre;

      /* The coordinates need to be chosen in [-radius, radius].  This set
         has (2 radius + 1) entries.  */
      const HexCoord::IntT xOffs = rnd.NextInt (2 * radius + 1) - radius;
      const HexCoord::IntT yOffs = rnd.NextInt (2 * radius + 1) - radius;

      res += HexCoord (xOffs, yOffs);

      if (HexCoord::DistanceL1 (res, centre) <= radius)
        {
          VLOG (2) << "Found suitable point: " << res;
          return res;
        }

      VLOG (2) << "Trial point " << res << " is out of range, retrying...";
    }
}

} // anonymous namespace

HexCoord
ChooseSpawnLocation (const HexCoord& centre, const HexCoord::IntT radius,
                     const Faction f, xaya::Random& rnd,
                     const DynObstacles& dyn, const BaseMap& map)
{
  const HexCoord ringCentre = RandomSpawnLocation (centre, radius, rnd);

  /* Starting from the ring centre, try L1 rings of increasing sizes (i.e.
     tiles with increasing L1 distance) until one is good for placement.  */
  for (HexCoord::IntT ringRad = 0; ; ++ringRad)
    {
      const L1Ring ring(ringCentre, ringRad);

      bool foundOnMap = false;
      for (const auto& pos : ring)
        {
          if (!map.IsOnMap (pos))
            continue;
          foundOnMap = true;

          if (map.IsPassable (pos) && dyn.IsPassable (pos, f))
            return pos;
        }

      /* If no coordinate on the current ring was even on the map, then we
         won't find a suitable spot anymore.  This is very, very, very unlikely
         to happen in practice, but it is still good to not continue in an
         endless loop.  */
      CHECK (foundOnMap);
    }
}

CharacterTable::Handle
SpawnCharacter (const std::string& owner, const Faction f,
                CharacterTable& tbl, DynObstacles& dyn, xaya::Random& rnd,
                const Context& ctx)
{
  VLOG (1)
      << "Spawning new character for " << owner
      << " in faction " << FactionToString (f) << "...";

  const auto& spawn
      = ctx.RoConfig ()->params ().spawn_areas ().at (FactionToString (f));
  const HexCoord pos
      = ChooseSpawnLocation (CoordFromProto (spawn.centre ()), spawn.radius (),
                             f, rnd, dyn, ctx.Map ());

  auto c = tbl.CreateNew (owner, f);
  c->SetPosition (pos);
  dyn.AddVehicle (pos, f);

  switch (f)
    {
    case Faction::RED:
      c->MutableProto ().set_vehicle ("rv st");
      break;
    case Faction::GREEN:
      c->MutableProto ().set_vehicle ("gv st");
      break;
    case Faction::BLUE:
      c->MutableProto ().set_vehicle ("bv st");
      break;
    default:
      LOG (FATAL)
          << "Unexpected faction for spawned character: "
          << static_cast<int> (f);
      break;
    }

  DeriveCharacterStats (*c, ctx);

  return c;
}

} // namespace pxd
