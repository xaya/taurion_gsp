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

#include "dynobstacles.hpp"

#include "buildings.hpp"

#include "database/character.hpp"

namespace pxd
{

DynObstacles::DynObstacles (const xaya::Chain c)
  : chain(c), vehicles(0), buildings(false)
{}

DynObstacles::DynObstacles (Database& db, const Context& ctx)
  : chain(ctx.Chain ()), vehicles(0), buildings(false)
{
  {
    CharacterTable tbl(db);
    tbl.ProcessAllPositions ([this] (const Database::IdT id,
                                     const HexCoord& pos, const Faction f)
      {
        AddVehicle (pos);
      });
  }

  {
    BuildingsTable tbl(db);
    auto res = tbl.QueryAll ();
    while (res.Step ())
      AddBuilding (*tbl.GetFromResult (res));
  }
}

bool
DynObstacles::AddBuilding (const std::string& type,
                           const proto::ShapeTransformation& trafo,
                           const HexCoord& pos,
                           std::vector<HexCoord>& shape)
{
  shape = GetBuildingShape (type, trafo, pos, chain);
  for (const auto& c : shape)
    {
      auto ref = buildings.Access (c);
      if (ref)
        return false;
      ref = true;
    }
  return true;
}

void
DynObstacles::AddBuilding (const Building& b)
{
  std::vector<HexCoord> shape;
  CHECK (AddBuilding (b.GetType (), b.GetProto ().shape_trafo (),
                      b.GetCentre (), shape))
      << "Error adding building " << b.GetId ();
}

} // namespace pxd
