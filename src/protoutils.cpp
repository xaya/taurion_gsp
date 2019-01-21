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
