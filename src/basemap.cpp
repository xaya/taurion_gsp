#include "basemap.hpp"

#include "hexagonal/coord.hpp"

namespace pxd
{

PathFinder::EdgeWeightFcn
BaseMap::GetEdgeWeights () const
{
  /* FIXME: For now, this is just a dummy function until we get the actual
     obstacle and map data.  */
  return [] (const HexCoord& from, const HexCoord& to) -> PathFinder::DistanceT
    {
      const HexCoord origin(0, 0);
      if (from == origin || to == origin)
        return PathFinder::NO_CONNECTION;
      return 1;
    };
}

} // namespace pxd
