#ifndef PXD_BASEMAP_HPP
#define PXD_BASEMAP_HPP

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

public:

  BaseMap () = default;

  BaseMap (const BaseMap&) = delete;
  void operator= (const BaseMap&) = delete;

  /**
   * Returns the edge-weight function for the basemap, to be used with path
   * finding on it.
   */
  PathFinder::EdgeWeightFcn GetEdgeWeights () const;

};

} // namespace pxd

#endif // PXD_BASEMAP_HPP
