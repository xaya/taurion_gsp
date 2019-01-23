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

  /** The default value, so that we can return it for out-of-range Get().  */
  const T defaultValue;

  /**
   * The underlying data as a flat vector.  It stores the hexagonal L1 range
   * compactly in a "somewhat rectangular" pattern:  It holds each row (fixed
   * y coordinate) after the other.  The first will have length range+1, the
   * next range+2, until the middle row of length 2*range+1.  After that,
   * the rows get smaller again until the bottom row has length range+1
   * again.
   *
   * With this structure, we do not waste any space at all (except for unset
   * elements in the map).  It is still reasonably fast to compute the index
   * for any given coordinate.
   */
  std::vector<T> data;

  /**
   * Returns the index into the flat vector at which a certain coordinate
   * will be found.  Returns -1 if the coordinate is out of range.
   */
  int GetIndex (const HexCoord& c) const;

  friend class RangeMapTests;

public:

  /**
   * Constructs the map for a given (fixed) L1 range around the centre and
   * with the given initial value for all cells.
   */
  explicit RangeMap (const HexCoord& c, HexCoord::IntT r, const T& val);

  RangeMap (const RangeMap<T>&) = delete;
  void operator= (const RangeMap<T>&) = delete;

  /**
   * Accesses and potentially modifies the element.  c must be within range
   * of the centre.
   */
  T& Access (const HexCoord& c);

  /**
   * Gives read-only access to the element (or the default value if the
   * element is out of range).
   */
  const T& Get (const HexCoord& c) const;

};

} // namespace pxd

#include "rangemap.tpp"

#endif // HEXAGONAL_RANGEMAP_HPP
