#include "basemap.hpp"

#include "tiledata.hpp"

#include <glog/logging.h>

namespace pxd
{

namespace
{

/** Number of bits in the packed passable array per character.  */
constexpr int BITS = 8;

/**
 * Returns the 0-based index into one of the arrays of tile data indexed
 * by the y coordinate within a non-0-based range.
 */
int
YArrayIndex (const int y)
{
  CHECK (y >= tiledata::minY && y <= tiledata::maxY);
  return y - tiledata::minY;
}

} // anonymous namespace

bool
BaseMap::IsOnMap (const HexCoord& c) const
{
  if (c.GetY () < tiledata::minY || c.GetY () > tiledata::maxY)
    return false;

  const int yInd = YArrayIndex (c.GetY ());
  return c.GetX () >= tiledata::minX[yInd]
          && c.GetX () <= tiledata::maxX[yInd];
}

bool
BaseMap::IsPassable (const HexCoord& c) const
{
  if (!IsOnMap (c))
    return false;

  const int yInd = YArrayIndex (c.GetY ());
  const unsigned char* bits
      = tiledata::obstacles::bitData
          + tiledata::obstacles::bitDataOffsetForY[yInd];

  const int xInd = c.GetX () - tiledata::minX[yInd];
  return (bits[xInd / BITS] & (1 << xInd % BITS));
}

PathFinder::EdgeWeightFcn
BaseMap::GetEdgeWeights () const
{
  return [this] (const HexCoord& from, const HexCoord& to)
            -> PathFinder::DistanceT
    {
      if (IsPassable (from) && IsPassable (to))
        return 1000;
      return PathFinder::NO_CONNECTION;
    };
}

} // namespace pxd
