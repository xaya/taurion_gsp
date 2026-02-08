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

/* Template implementation code for dynobstacles.hpp.  */

#include <glog/logging.h>

namespace pxd
{

inline bool
DynObstacles::IsBuilding (const HexCoord& c) const
{
  return buildings.Get (c);
}

inline bool
DynObstacles::HasVehicle (const HexCoord& c) const
{
  return vehicles.Get (c) > 0;
}

inline bool
DynObstacles::IsFree (const HexCoord& c) const
{
  return !IsBuilding (c) && !HasVehicle (c);
}

inline void
DynObstacles::AddVehicle (const HexCoord& c)
{
  vehicles.Set (c, vehicles.Get (c) + 1);
}

inline void
DynObstacles::RemoveVehicle (const HexCoord& c)
{
  const auto cnt = vehicles.Get (c);
  CHECK_GT (cnt, 0);
  vehicles.Set (c, cnt - 1);
}

} // namespace pxd
