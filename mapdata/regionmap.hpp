#ifndef MAPDATA_REGIONMAP_HPP
#define MAPDATA_REGIONMAP_HPP

#include "hexagonal/coord.hpp"

#include <cstdint>

namespace pxd
{

/**
 * Utility class for working with the region data of our basemap.  This can
 * mainly map coordinates to region IDs based on the embedded, compacted
 * data.  It can also find more geometrical data about a region, though,
 * like all other tiles in it.
 */
class RegionMap
{

public:

  /** Type for the ID of regions.  */
  using IdT = uint32_t;

  RegionMap ();

  RegionMap (const RegionMap&) = delete;
  void operator= (const RegionMap&) = delete;

  /**
   * Returns the region of the given coordinate.  Must not be called for
   * coordinates outside of the base map.
   */
  IdT GetRegionForTile (const HexCoord& c) const;

};

} // namespace pwd

#endif // MAPDATA_REGIONMAP_HPP
