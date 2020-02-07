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

#include "buildings.hpp"

#include "protoutils.hpp"

#include "proto/config.pb.h"
#include "proto/roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

std::vector<HexCoord>
GetBuildingShape (const Building& b)
{
  const auto& buildings = RoConfigData ().building_types ();
  const auto mit = buildings.find (b.GetType ());
  CHECK (mit != buildings.end ())
      << "Building " << b.GetId () << " has undefined type: " << b.GetType ();

  const auto centre = b.GetCentre ();
  const auto& trafo = b.GetProto ().shape_trafo ();

  std::vector<HexCoord> res;
  res.reserve (mit->second.shape_tiles ().size ());
  for (const auto& pbTile : mit->second.shape_tiles ())
    {
      HexCoord c = CoordFromProto (pbTile);
      c = c.RotateCW (trafo.rotation_steps ()); 
      c += centre;
      res.push_back (c);
    }

  return res;
}

void
InitialiseBuildings (Database& db)
{
  LOG (INFO) << "Adding initial ancient buildings to the map...";
  BuildingsTable tbl(db);

  auto h = tbl.CreateNew ("obelisk1", "", Faction::ANCIENT);
  h->SetCentre (HexCoord (-125, 810));
  h.reset ();

  h = tbl.CreateNew ("obelisk2", "", Faction::ANCIENT);
  h->SetCentre (HexCoord (-1'301, 902));
  h.reset ();

  h = tbl.CreateNew ("obelisk3", "", Faction::ANCIENT);
  h->SetCentre (HexCoord (-637, -291));
  h.reset ();
}

} // namespace pxd
