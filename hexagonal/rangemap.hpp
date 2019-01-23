#ifndef HEXAGONAL_RANGEMAP_HPP
#define HEXAGONAL_RANGEMAP_HPP

#include "coord.hpp"

#include <cstddef>
#include <unordered_map>

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

  /** Default value (so we can add it to the map if necessary).  */
  const T defaultValue;

  /**
   * The underlying data store.  For now, this is an unordered_map just to
   * transition code here.  This will be made more efficient in the future.
   */
  std::unordered_map<HexCoord, T> data;

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
