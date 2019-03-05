#ifndef MAPDATA_REGIONMAP_HPP
#define MAPDATA_REGIONMAP_HPP

#include "hexagonal/coord.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace pxd
{

/**
 * Maps tile coordinates to their corresponding region ID (based on static
 * data that encodes the basemap).
 */
class RegionMap
{

protected:

  RegionMap () = default;  

  RegionMap (const RegionMap&) = delete;
  void operator= (const RegionMap&) = delete;

public:

  virtual ~RegionMap () = default;

  /** Type for the ID of regions.  */
  using IdT = uint32_t;

  /**
   * Returns the region of the given coordinate.  Must not be called for
   * coordinates outside of the base map.
   */
  virtual IdT GetRegionForTile (const HexCoord& c) const = 0;

};

/**
 * Constructs a RegionMap loading all data from the given file into memory.
 */
std::unique_ptr<RegionMap> NewInMemoryRegionMap (const std::string& filename);

/**
 * Constructs a RegionMap that uses a stream and seeks in it to load data.
 * This uses almost no memory but is very slow!
 */
std::unique_ptr<RegionMap> NewStreamRegionMap (const std::string& filename);

/**
 * Constructs a RegionMap that mmap's the data file.
 */
std::unique_ptr<RegionMap> NewMemMappedRegionMap (const std::string& filename);

/**
 * Constructs a RegionMap that uses the embedded, compacted data.
 */
std::unique_ptr<RegionMap> NewCompactRegionMap ();

} // namespace pwd

#endif // MAPDATA_REGIONMAP_HPP
