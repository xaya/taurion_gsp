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

/* Template implementation code for dynobstacles.hpp.  */

#include <glog/logging.h>

namespace pxd
{

inline DynTiles<bool>&
DynObstacles::FactionVehicles (const Faction f)
{
  switch (f)
    {
    case Faction::RED:
      return red;
    case Faction::GREEN:
      return green;
    case Faction::BLUE:
      return blue;
    default:
      LOG (FATAL) << "Invalid vehicle faction: " << static_cast<int> (f);
    }
}

inline bool
DynObstacles::IsPassable (const HexCoord& c, const Faction f) const
{
  return !buildings.Get (c) && !FactionVehicles (f).Get (c);
}

inline bool
DynObstacles::IsFree (const HexCoord& c) const
{
  return !buildings.Get (c) && !red.Get (c) && !green.Get (c) && !blue.Get (c);
}

inline void
DynObstacles::AddVehicle (const HexCoord& c, const Faction f)
{
  auto ref = FactionVehicles (f).Access (c);
  CHECK (!ref);
  ref = true;
}

inline void
DynObstacles::RemoveVehicle (const HexCoord& c, const Faction f)
{
  auto ref = FactionVehicles (f).Access (c);
  CHECK (ref);
  ref = false;
}

} // namespace pxd
