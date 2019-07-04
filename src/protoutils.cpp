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

#include "protoutils.hpp"

namespace pxd
{

proto::HexCoord
CoordToProto (const HexCoord& c)
{
  proto::HexCoord res;
  res.set_x (c.GetX ());
  res.set_y (c.GetY ());
  return res;
}

HexCoord
CoordFromProto (const proto::HexCoord& pb)
{
  return HexCoord (pb.x (), pb.y ());
}

void
SetRepeatedCoords (const std::vector<HexCoord>& coords,
                   google::protobuf::RepeatedPtrField<proto::HexCoord>& field)
{
  field.Clear ();
  for (const auto& c : coords)
    *field.Add () = CoordToProto (c);
}

} // namespace pxd
