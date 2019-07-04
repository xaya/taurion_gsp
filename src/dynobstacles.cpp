/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#include "dynobstacles.hpp"

#include "database/character.hpp"

namespace pxd
{

DynObstacles::DynObstacles (Database& db)
  : red(false), green(false), blue(false)
{
  CharacterTable tbl(db);
  auto res = tbl.QueryAll ();
  while (res.Step ())
    {
      auto c = tbl.GetFromResult (res);
      AddVehicle (c->GetPosition (), c->GetFaction ());
    }
}

DynTiles<bool>&
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
      LOG (FATAL) << "Unknown faction: " << static_cast<int> (f);
    }
}

bool
DynObstacles::IsPassable (const HexCoord& c, const Faction f) const
{
  return !FactionVehicles (f).Get (c);
}

void
DynObstacles::AddVehicle (const HexCoord& c, const Faction f)
{
  auto ref = FactionVehicles (f).Access (c);
  CHECK (!ref);
  ref = true;
}

void
DynObstacles::RemoveVehicle (const HexCoord& c, const Faction f)
{
  auto ref = FactionVehicles (f).Access (c);
  CHECK (ref);
  ref = false;
}

} // namespace pxd
