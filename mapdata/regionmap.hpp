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
