#ifndef HEXAGONAL_COORD_HPP
#define HEXAGONAL_COORD_HPP

#include <cstdint>

namespace pxd
{

/**
 * A hexagonal coordinate, based on "axial coordinates".  It can also enumerate
 * its neighbours, so that path-finding is enabled.
 *
 * See https://www.redblobgames.com/grids/hexagons/ for some basic discussion
 * of the underlying theory.
 */
class HexCoord final
{

public:

  /** Integer type used to hold the coordinates.  */
  using IntT = int16_t;

private:

  /** The "first" axial coordinate.  */
  IntT x;
  /** The "second" axial coordinate.  */
  IntT y;

  class NeighbourList;

public:

  inline explicit HexCoord (const IntT xx, const IntT yy)
    : x(xx), y(yy)
  {}

  inline HexCoord ()
    : x(0), y(0)
  {}

  HexCoord (const HexCoord&) = default;
  HexCoord& operator= (const HexCoord&) = default;

  friend inline bool
  operator== (const HexCoord& a, const HexCoord& b)
  {
    return a.x == b.x && a.y == b.y;
  }

  friend inline bool
  operator!= (const HexCoord& a, const HexCoord& b)
  {
    return !(a == b);
  }

  friend inline bool
  operator< (const HexCoord& a, const HexCoord& b)
  {
    if (a.x != b.x)
      return a.x < b.x;
    return a.y < b.y;
  }

  inline IntT
  GetX () const
  {
    return x;
  }

  inline IntT
  GetY () const
  {
    return y;
  }

  /**
   * Computes and returns the matching z coordinate in cubic hex coordinates.
   */
  inline IntT
  GetZ () const
  {
    return -x - y;
  }

  /**
   * Returns an "opaque" object that can be iterated over to yield the
   * neighbouring hex cells.
   */
  NeighbourList Neighbours () const;

  /**
   * Computes the L1 distance to the origin, i.e. on what "ring" this coordinate
   * is placed on.
   */
  inline IntT GetRing () const
  {
    return DistanceL1 (*this, HexCoord (0, 0));
  }

  /**
   * Computes the L1 distance between two coordinates.
   */
  static IntT DistanceL1 (const HexCoord& a, const HexCoord& b);

};

/**
 * Dummy class that gives access (and can be iterated over) to the neighbours
 * of a certain hex cell.
 */
class HexCoord::NeighbourList final
{

private:

  /** The underlying hex cell.  */
  const HexCoord& centre;

  class ConstIterator;

public:

  inline explicit NeighbourList (const HexCoord& c)
    : centre(c)
  {}

  NeighbourList (NeighbourList&&) = default;

  NeighbourList () = delete;
  NeighbourList (const NeighbourList&) = delete;
  void operator= (const NeighbourList&) = delete;

  ConstIterator begin () const;
  ConstIterator end () const;

};

/**
 * Simple iterator for the neighbouring tiles of a given hex coord.
 */
class HexCoord::NeighbourList::ConstIterator final
{

private:

  /** Centre tile around which we iterate.  */
  const HexCoord& centre;

  /**
   * Next neighbour to return.  This starts at zero and counts up to six,
   * at which point we have reached the end.
   */
  uint8_t next = 0;

public:

  inline explicit ConstIterator (const HexCoord& c, const bool end)
    : centre(c)
  {
    if (end)
      next = 6;
  }

  ConstIterator () = delete;

  ConstIterator (const ConstIterator&) = default;
  ConstIterator& operator= (const ConstIterator&) = default;

  friend inline bool
  operator== (const ConstIterator& a, const ConstIterator& b)
  {
    return &a.centre == &b.centre && a.next == b.next;
  }

  friend inline bool
  operator!= (const ConstIterator& a, const ConstIterator& b)
  {
    return !(a == b);
  }

  void operator++ ();
  HexCoord operator* () const;

};

} // namespace pxd

#endif // HEXAGONAL_COORD_HPP
