#ifndef MAPDATA_REGIONMAP_HPP
#define MAPDATA_REGIONMAP_HPP

#include "hexagonal/coord.hpp"

#include <cstdint>
#include <set>

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

  /** Region ID value returned for out-of-map coordinates.  */
  static constexpr IdT OUT_OF_MAP = static_cast<IdT> (-1);

  RegionMap ();

  RegionMap (const RegionMap&) = delete;
  void operator= (const RegionMap&) = delete;

  /**
   * Returns the region ID of the given coordinate.  Returns OUT_OF_MAP if the
   * given coordinate is not on the base map itself.
   */
  IdT GetRegionId (const HexCoord& c) const;

  /**
   * Returns the region ID and the set of all coordinates of that region
   * for the given coordinate.  Must not be called for out-of-map coordinates.
   */
  std::set<HexCoord> GetRegionShape (const HexCoord& c, IdT& id) const;

};

} // namespace pwd

#endif // MAPDATA_REGIONMAP_HPP
