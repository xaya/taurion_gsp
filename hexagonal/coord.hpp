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

#ifndef HEXAGONAL_COORD_HPP
#define HEXAGONAL_COORD_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>

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

  friend class std::hash<HexCoord>;

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

  friend inline std::ostream&
  operator<< (std::ostream& out, const HexCoord& c)
  {
    out << "(" << c.x << ", " << c.y << ")";
    return out;
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

  void
  operator+= (const HexCoord& delta)
  {
    x += delta.GetX ();
    y += delta.GetY ();
  }

  friend HexCoord
  operator* (const IntT f, const HexCoord& c)
  {
    return HexCoord (f * c.GetX (), f * c.GetY ());
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

namespace std
{

/**
 * Defines a specialisation of std::hash for HexCoord, so that it can be used
 * as a key in std::unordered_map and the likes.
 */
template <>
  struct hash<pxd::HexCoord>
{

  inline size_t
  operator() (const pxd::HexCoord& c) const
  {
    /* Just combine the two coordinates with the x coordinate shifted half-way
       through the size_t.  Since HexCoord::IntT is typically smaller than
       size_t, this yields a hash function that should not have collisions
       at all.  Since IntT is signed, we have to "shift" the value up to
       an unsigned range first.  */

    constexpr size_t offs = -std::numeric_limits<pxd::HexCoord::IntT>::min ();
    static_assert (offs > 0, "Unexpected minimum for IntT");

    size_t res = c.x + offs;
    res <<= sizeof (res) * 4;
    res ^= c.y + offs;
    return res;
  }

};

} // namespace std

#endif // HEXAGONAL_COORD_HPP
