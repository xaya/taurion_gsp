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

#include "tiledata.hpp"

#include "hexagonal/coord.hpp"

#include <array>

namespace pxd
{

namespace dyntiles
{

/**
 * Size of each "bucket" of values.  We use one array
 * for each bucket, and have a larger array of arrays.  That way,
 * we can initialise each bucket only when needed (i.e. it is changed
 * from the default value), which improves performance for mostly sparse
 * data (e.g. dynamic obstacles from vehicles).
 */
constexpr size_t BUCKET_SIZE = (1 << 16);

/** Number of buckets to cover (at least) numTiles.  */
constexpr size_t NUM_BUCKETS = tiledata::numTiles / BUCKET_SIZE + 1;
static_assert (BUCKET_SIZE * NUM_BUCKETS >= tiledata::numTiles,
               "number of buckets is too small to cover all tiles");

/**
 * Custom implementation of an "optional object" of the given type.
 * We use that internally to represent bucket arrays in DynTiles.
 */
template <typename T> class Optional;

/**
 * Fixed array of N entries of type T like std::array, but this class
 * has a specialisation to bool that uses std::bitset instead.
 */
template <typename T, size_t N> class BucketArray;

} // namespace dyntiles

/**
 * Dynamic map of each tile to a value with given type.  This holds storage
 * exactly for each map tile, unlike FullRangeMap which overestimates the
 * data requirement (but does not depend on the exact map structure).
 *
 * For boolean type, this is memory efficient and stores them as individual
 * bits rather than bytes (using std::bitset under the hood).
 */
template <typename T>
  class DynTiles
{

private:

  /** The type of array for our buckets.  */
  using Array = dyntiles::BucketArray<T, dyntiles::BUCKET_SIZE>;

  /** The default value.  */
  const T defaultValue;

  /**
   * The underlying data, as a vector of vectors.  Each entry here corresponds
   * to BUCKET_SIZE tiles; it may be empty instead, in which case we assume
   * that all of those tiles are still at the default value.
   */
  std::array<dyntiles::Optional<Array>, dyntiles::NUM_BUCKETS> data;

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
  typename Array::reference Access (const HexCoord& c);

  /**
   * Gives read-only access to the element.  c must be on the map.
   */
  typename Array::const_reference Get (const HexCoord& c) const;

};

} // namespace pxd

#include "dyntiles.tpp"

#endif // MAPDATA_DYNTILES_HPP
