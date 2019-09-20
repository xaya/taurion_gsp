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

/* Template implementation code for dyntiles.hpp.  */

#include <glog/logging.h>

#include <bitset>

namespace pxd
{

namespace dyntiles
{

/**
 * Computes the index into our abstract data vector at which a certain
 * coordinate will be found.  The abstract data vector is the assumed
 * array of all tiles, stored row-by-row.
 */
inline size_t
GetIndex (const HexCoord& c)
{
  const auto x = c.GetX ();
  const auto y = c.GetY ();

  using namespace tiledata;

  CHECK (y >= minY && y <= maxY);
  const auto yInd = y - minY;

  CHECK (x >= minX[yInd] && x <= maxX[yInd]);
  return offsetForY[yInd] + x - minX[yInd];
}

/**
 * Computes both the bucket number and index within the bucket for the
 * given overall index into the abstract data vector.
 */
inline void
GetBuckets (const size_t fullIndex, size_t& bucket, size_t& within)
{
  bucket = fullIndex / BUCKET_SIZE;
  within = fullIndex % BUCKET_SIZE;
  CHECK_EQ (BUCKET_SIZE * bucket + within, fullIndex);
}

template <typename T>
  class Optional
{

private:

  /** The value if present.  */
  T* value = nullptr;

public:

  Optional () = default;

  ~Optional ()
  {
    /* For some reason, it makes a huge performance difference (e.g. in the
       DynTilesBoolConstruction benchmark) to explicitly check for the
       pointer being null before the delete.  */
    if (value != nullptr)
      delete value;
  }

  Optional (const Optional<T>&) = delete;
  void operator= (const Optional<T>&) = delete;

  /**
   * Extracts the value if it is present, returning nullptr if not.
   */
  T*
  Get ()
  {
    return value;
  }

  /**
   * Extracts the value if it is present, returning nullptr if not.
   */
  const T*
  Get () const
  {
    return value;
  }

  /**
   * Ensures that there is a value.  If there is not, it is
   * default-constructed.  Returns true if the value was constructed.
   */
  bool
  MaybeConstruct ()
  {
    if (value != nullptr)
      return false;

    value = new T ();
    return true;
  }

};

/* By default, BucketArray is just a std::array.  */
template <typename T, size_t N>
  class BucketArray : public std::array<T, N>
{};

/* Specialisation of BucketArray to type bool, which is stored more efficiently
   in a std::bitset instead of std::array.  We make it look like std::array
   as much as necessary, though.  */
template <size_t N>
  class BucketArray<bool, N> : public std::bitset<N>
{

public:

  /* Just pass bool by value back from Get().  */
  using const_reference = bool;

  void
  fill (const bool val)
  {
    if (val)
      this->set ();
    else
      this->reset ();
  }

};

} // namespace dyntiles

template <typename T>
  DynTiles<T>::DynTiles (const T& val)
  : defaultValue(val)
{}

template <typename T>
  inline typename DynTiles<T>::Array::reference
  DynTiles<T>::Access (const HexCoord& c)
{
  size_t bucket, within;
  dyntiles::GetBuckets (dyntiles::GetIndex (c), bucket, within);

  auto& part = data[bucket];
  if (part.MaybeConstruct ())
    part.Get ()->fill (defaultValue);

  return (*part.Get ())[within];
}

template <typename T>
  inline typename DynTiles<T>::Array::const_reference
  DynTiles<T>::Get (const HexCoord& c) const
{
  size_t bucket, within;
  dyntiles::GetBuckets (dyntiles::GetIndex (c), bucket, within);

  const auto& part = data[bucket];
  if (part.Get () == nullptr)
    return defaultValue;

  return (*part.Get ())[within];
}

} // namespace pxd
