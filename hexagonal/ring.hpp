#ifndef HEXAGONAL_RING_HPP
#define HEXAGONAL_RING_HPP

#include "coord.hpp"

namespace pxd
{

/**
 * Utility class that represents a "ring" of all tiles in a certain L1
 * distance from a centre.  The main use of this is that it also allows
 * to enumerate those tiles.
 */
class L1Ring
{

private:

  /** The ring's centre.  */
  const HexCoord centre;

  /** The ring's L1 radius.  */
  const HexCoord::IntT radius;

  class ConstIterator;

public:

  explicit L1Ring (const HexCoord& c, const HexCoord::IntT r);

  L1Ring () = delete;
  L1Ring (const L1Ring&) = delete;
  void operator= (const L1Ring&) = delete;

  ConstIterator begin () const;
  ConstIterator end () const;

};

/**
 * Iterator through all coordinates in an L1 ring.
 */
class L1Ring::ConstIterator final
{

private:

  /** The radius of this ring.  */
  const HexCoord::IntT radius;

  /** The "current" coordinate of the iterator.  */
  HexCoord cur;

  /** Number of increments left before we reach the next corner.  */
  unsigned nextCornerIn;

  /** The "side" we are currently on (0-5, 6 means end).  */
  unsigned side;

public:

  explicit ConstIterator (const L1Ring& r, bool end);

  ConstIterator (const ConstIterator&) = default;

  ConstIterator () = delete;
  void operator= (const ConstIterator&) = delete;

  friend bool
  operator== (const ConstIterator& a, const ConstIterator& b)
  {
    return a.radius == b.radius && a.cur == b.cur
              && a.nextCornerIn == b.nextCornerIn && a.side == b.side;
  }

  friend bool
  operator!= (const ConstIterator& a, const ConstIterator& b)
  {
    return !(a == b);
  }

  void operator++ ();
  const HexCoord& operator* () const;

};

} // namespace pxd

#endif // HEXAGONAL_RING_HPP
