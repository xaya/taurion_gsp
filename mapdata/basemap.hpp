#ifndef MAPDATA_BASEMAP_HPP
#define MAPDATA_BASEMAP_HPP

#include "regionmap.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

namespace pxd
{

/**
 * Base data for the map in the game.  It wraps the underlying static data,
 * knowing which tiles are within the range of the map, obstacles or what
 * type of regions they are.
 */
class BaseMap
{

private:

  /** RegionMap instance that is exposed as part of the BaseMap.  */
  RegionMap rm;

  /**
   * Returns true if the given coordinate is passable according to the
   * obstacle layer data.
   */
  bool IsPassable (const HexCoord& c) const;

  friend class BaseMapTests;

public:

  BaseMap ();

  BaseMap (const BaseMap&) = delete;
  void operator= (const BaseMap&) = delete;

  /**
   * Returns true if the given coordinate is "on the map".
   */
  bool IsOnMap (const HexCoord& c) const;

  const RegionMap&
  Regions () const
  {
    return rm;
  }

  /**
   * Returns the edge-weight function for the basemap, to be used with path
   * finding on it.
   */
  PathFinder::EdgeWeightFcn GetEdgeWeights () const;

};

} // namespace pxd

#endif // MAPDATA_BASEMAP_HPP
