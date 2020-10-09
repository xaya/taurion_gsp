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

#ifndef PXD_PROTOUTILS_HPP
#define PXD_PROTOUTILS_HPP

#include "hexagonal/coord.hpp"
#include "proto/geometry.pb.h"

#include <google/protobuf/repeated_field.h>

#include <vector>

namespace pxd
{

/**
 * Converts a HexCoord to the corresponding protocol buffer.
 */
proto::HexCoord CoordToProto (const HexCoord& c);

/**
 * Converts a HexCoord in protocol buffer form to the real object.
 */
HexCoord CoordFromProto (const proto::HexCoord& pb);

/**
 * Adds a vector of coordinates to a repeated field in the protocol buffer.
 */
void AddRepeatedCoords (
    const std::vector<HexCoord>& coords,
    google::protobuf::RepeatedPtrField<proto::HexCoord>& field);

} // namespace pxd

#endif // PXD_PROTOUTILS_HPP
