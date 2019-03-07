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

private:

  /**
   * Computes the region of the given coordinate and returns that together
   * with extra data as needed for other methods.  Returns OUT_OF_MAP for
   * coordinates that are not on the basemap at all.
   *
   * In addition to the ID, this also returns the lowest and largest
   * x coordinate in the row around c that still is in the same region.
   * This is free with the compacted data format we use, and can be used
   * for GetRegionShape.
   */
  IdT GetRegionInternal (const HexCoord& c, HexCoord::IntT& lowerX,
                         HexCoord::IntT& upperX) const;

public:

  /** Region ID value returned for out-of-map coordinates.  */
  static constexpr IdT OUT_OF_MAP = static_cast<IdT> (-1);

  RegionMap ();

  RegionMap (const RegionMap&) = delete;
  void operator= (const RegionMap&) = delete;

  /**
   * Returns the region ID of the given coordinate.  Returns OUT_OF_MAP if the
   * given coordinate is not on the base map itself.
   */
  IdT
  GetRegionId (const HexCoord& c) const
  {
    HexCoord::IntT lowerX, upperX;
    return GetRegionInternal (c, lowerX, upperX);
  }

  /**
   * Returns the region ID and the set of all coordinates of that region
   * for the given coordinate.  Must not be called for out-of-map coordinates.
   */
  std::set<HexCoord> GetRegionShape (const HexCoord& c, IdT& id) const;

};

} // namespace pwd

#endif // MAPDATA_REGIONMAP_HPP
