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

#ifndef HEXAGONAL_RANGEMAP_HPP
#define HEXAGONAL_RANGEMAP_HPP

#include "coord.hpp"

#include <cstddef>
#include <vector>

namespace pxd
{

/**
 * An efficient map of HexCoordinates in some L1-range around a centre to
 * other values.  This is used by the path finder to store distances.
 */
template <typename T>
  class RangeMap
{

private:

  /** The centre of the map.  */
  const HexCoord centre;

  /** The range around the centre that this is for.  */
  const HexCoord::IntT range;

  /**
   * The underlying data as a flat vector.  It stores the hexagonal L1 range
   * in a rectangular pattern.  This is quick to access, although it wastes
   * "some" space (but that should not matter much).
   */
  std::vector<T> data;

  /**
   * Returns the index into the flat vector at which a certain coordinate
   * will be found.  c must be in range.
   */
  int GetIndex (const HexCoord& c) const;

public:

  /**
   * Constructs the map for a given (fixed) L1 range around the centre and
   * with the given initial value for all cells.
   */
  explicit RangeMap (const HexCoord& c, HexCoord::IntT r, const T& val);

  RangeMap (const RangeMap<T>&) = delete;
  void operator= (const RangeMap<T>&) = delete;

  /**
   * Checks if the given coordinate is in-range for the map.
   */
  bool IsInRange (const HexCoord& c) const;

  /**
   * Accesses and potentially modifies the element.  c must be within range
   * of the centre.
   */
  typename std::vector<T>::reference Access (const HexCoord& c);

  /**
   * Gives read-only access to the element.  c must be within range of
   * the centre.
   */
  typename std::vector<T>::const_reference Get (const HexCoord& c) const;

};

/**
 * Specialised implementation of RangeMap that is able to hold all of the
 * map tiles at once.  This uses a lot of memory, but can be useful in
 * specific situations (e.g. tests).
 *
 * DynTiles from mapdata is a more efficient version of such a map, which
 * really just stores as many tiles as necessary.  It depends on the actual
 * map data layout, though.  Thus it should be preferred in real production
 * use for the game backend, but tests (and the map processing code itself)
 * can still make good use of FullRangeMap instead.
 */
template <typename T>
  class FullRangeMap : public RangeMap<T>
{

private:

  /** L1 range enough to cover the whole map around the origin.  */
  static constexpr HexCoord::IntT FULL_L1RANGE = 7000;

public:

  explicit FullRangeMap (const T& val)
    : RangeMap<T> (HexCoord (), FULL_L1RANGE, val)
  {}

};

} // namespace pxd

#include "rangemap.tpp"

#endif // HEXAGONAL_RANGEMAP_HPP
