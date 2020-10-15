/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#ifndef MAPDATA_SPARSEMAP_HPP
#define MAPDATA_SPARSEMAP_HPP

#include "dyntiles.hpp"

#include "hexagonal/coord.hpp"

#include <unordered_map>

namespace pxd
{

/**
 * Sparse map from hex coordinates to some associated value.  This uses an
 * underlying bitmap for each tile to quickly determine whether or not
 * a given tile is actually in the map or not, and only looks up the actual
 * value if it is.
 */
template <typename T>
  class SparseTileMap
{

private:

  /** The default value, which corresponds to missing entries.  */
  const T defaultValue;

  /** The density map.  */
  DynTiles<bool> density;

  /** The actual map from existing tiles to values.  */
  std::unordered_map<HexCoord, T> values;

  friend class SparseMapTests;

public:

  /**
   * Constructs the map with all elements set to the given value.
   */
  explicit SparseTileMap (const T& val);

  SparseTileMap () = delete;
  SparseTileMap (const SparseTileMap&) = delete;
  void operator= (const SparseTileMap&) = delete;

  /**
   * Returns the value associated with a coordinate (or the default value
   * if the coordinate is not set).
   */
  const T& Get (const HexCoord& c) const;

  /**
   * Sets the value associated with a coordinate.  If the value we are setting
   * to is the default value, removes the element entirely.
   */
  void Set (const HexCoord& c, const T& val);

};

} // namespace pxd

#include "sparsemap.tpp"

#endif // MAPDATA_SPARSEMAP_HPP
