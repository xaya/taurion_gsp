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

} // namespace pxd
