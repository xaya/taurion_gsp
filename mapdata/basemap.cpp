#include "basemap.hpp"

#include "obstacles.hpp"

#include <glog/logging.h>

namespace pxd
{

namespace
{

/**
 * Returns the 0-based index into one of the arrays of obstacle data indexed
 * by the y coordinate within a non-0-based range.
 */
int
ObstacleYArrayIndex (const int y)
{
  CHECK (y >= obstacles::minY && y <= obstacles::maxY);
  return y - obstacles::minY;
}

} // anonymous namespace

bool
BaseMap::IsOnMap (const HexCoord& c) const
{
  if (c.GetY () < obstacles::minY || c.GetY () > obstacles::maxY)
    return false;

  const int yInd = ObstacleYArrayIndex (c.GetY ());
  return c.GetX () >= obstacles::minX[yInd]
          && c.GetX () <= obstacles::maxX[yInd];
}

bool
BaseMap::IsPassable (const HexCoord& c) const
{
  if (!IsOnMap (c))
    return false;

  const int yInd = ObstacleYArrayIndex (c.GetY ());
  const unsigned char* bits
      = obstacles::bitData + obstacles::bitDataOffsetForY[yInd];

  const int xInd = c.GetX () - obstacles::minX[yInd];
  return (bits[xInd / 8] & (1 << xInd % 8));
}

PathFinder::EdgeWeightFcn
BaseMap::GetEdgeWeights () const
{
  return [this] (const HexCoord& from, const HexCoord& to)
            -> PathFinder::DistanceT
    {
      if (IsPassable (from) && IsPassable (to))
        return 1;
      return PathFinder::NO_CONNECTION;
    };
}

} // namespace pxd
