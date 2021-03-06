/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#ifndef MAPDATA_BASEMAP_HPP
#define MAPDATA_BASEMAP_HPP

#include "regionmap.hpp"
#include "safezones.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"
#include "proto/roconfig.hpp"

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

  /** RoConfig data.  */
  const RoConfig cfg;

  /** RegionMap instance that is exposed as part of the BaseMap.  */
  const RegionMap rm;

  /** SafeZones instance used.  */
  const pxd::SafeZones sz;

public:

  explicit BaseMap (const xaya::Chain c);

  BaseMap () = delete;
  BaseMap (const BaseMap&) = delete;
  void operator= (const BaseMap&) = delete;

  /**
   * Returns true if the given coordinate is "on the map".
   */
  bool IsOnMap (const HexCoord& c) const;

  /**
   * Returns true if the given coordinate is passable according to the
   * obstacle layer data.
   */
  bool IsPassable (const HexCoord& c) const;

  const RegionMap&
  Regions () const
  {
    return rm;
  }

  const pxd::SafeZones&
  SafeZones () const
  {
    return sz;
  }

  /**
   * Returns the edge-weight for the basemap, to be used with path
   * finding on it.
   */
  PathFinder::DistanceT GetEdgeWeight (const HexCoord& from,
                                       const HexCoord& to) const;

};

} // namespace pxd

#include "basemap.tpp"

#endif // MAPDATA_BASEMAP_HPP
