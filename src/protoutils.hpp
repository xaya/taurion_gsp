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
 * Sets a repeated coordinate field in the protocol buffer to the given
 * vector of coordinates.
 */
void SetRepeatedCoords (
    const std::vector<HexCoord>& coords,
    google::protobuf::RepeatedPtrField<proto::HexCoord>& field);

} // namespace pxd

#endif // PXD_PROTOUTILS_HPP
