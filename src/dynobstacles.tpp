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

inline SparseTileMap<unsigned>&
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
DynObstacles::IsBuilding (const HexCoord& c) const
{
  return buildings.Get (c);
}

inline bool
DynObstacles::HasVehicle (const HexCoord& c, const Faction f) const
{
  return FactionVehicles (f).Get (c) > 0;
}

inline bool
DynObstacles::HasVehicle (const HexCoord& c) const
{
  return red.Get (c) > 0 || green.Get (c) > 0 || blue.Get (c) > 0;
}

inline bool
DynObstacles::IsFree (const HexCoord& c) const
{
  return !buildings.Get (c) && !HasVehicle (c);
}

inline void
DynObstacles::AddVehicle (const HexCoord& c, const Faction f)
{
  auto& fv = FactionVehicles (f);
  fv.Set (c, fv.Get (c) + 1);
}

inline void
DynObstacles::RemoveVehicle (const HexCoord& c, const Faction f)
{
  auto& fv = FactionVehicles (f);
  const auto cnt = fv.Get (c);
  CHECK_GT (cnt, 0);
  fv.Set (c, cnt - 1);
}

} // namespace pxd
