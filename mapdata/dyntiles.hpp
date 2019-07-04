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

#ifndef MAPDATA_DYNTILES_HPP
#define MAPDATA_DYNTILES_HPP

#include "hexagonal/coord.hpp"

#include <vector>

namespace pxd
{

/**
 * Dynamic map of each tile to a value with given type.  This holds storage
 * exactly for each map tile, unlike FullRangeMap which overestimates the
 * data requirement (but does not depend on the exact map structure).
 *
 * Note that the data is stored in a std::vector behind the scenes, so this
 * is memory-efficient also for bool.
 */
template <typename T>
  class DynTiles
{

private:

  /** The underlying data.  It is stored row-by-row in the vector.  */
  std::vector<T> data;

  /**
   * Computes the index into the data vector at which a certain coordinate
   * will be found.
   */
  static size_t GetIndex (const HexCoord& c);

public:

  /**
   * Constructs the map with all elements set to the given value.
   */
  explicit DynTiles (const T& val);

  DynTiles () = delete;
  DynTiles (const DynTiles&) = delete;
  void operator= (const DynTiles&) = delete;

  /**
   * Accesses and potentially modifies the element.  c must be on the map.
   */
  typename std::vector<T>::reference Access (const HexCoord& c);

  /**
   * Gives read-only access to the element.  c must be on the map.
   */
  typename std::vector<T>::const_reference Get (const HexCoord& c) const;

};

} // namespace pxd

#include "dyntiles.tpp"

#endif // MAPDATA_DYNTILES_HPP
