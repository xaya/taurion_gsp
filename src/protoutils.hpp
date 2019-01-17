#ifndef PXD_PROTOUTILS_HPP
#define PXD_PROTOUTILS_HPP

#include "hexagonal/coord.hpp"
#include "proto/geometry.pb.h"

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

} // namespace pxd

#endif // PXD_PROTOUTILS_HPP
