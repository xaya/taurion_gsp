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

BaseMap::BaseMap ()
{
  CHECK_EQ (&blob_obstacles_end - &blob_obstacles_start,
            tiledata::obstacles::bitDataSize);
}

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
      = &blob_obstacles_start + tiledata::obstacles::bitDataOffsetForY[yInd];

  const int xInd = c.GetX () - tiledata::minX[yInd];
  return (bits[xInd / BITS] & (1 << xInd % BITS));
}

PathFinder::DistanceT
BaseMap::GetEdgeWeight (const HexCoord& from, const HexCoord& to) const
{
  if (IsPassable (from) && IsPassable (to))
    return 1000;

  return PathFinder::NO_CONNECTION;
}

} // namespace pxd
