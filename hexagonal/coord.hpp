/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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
#include <iostream>

namespace pxd
{

/**
 * A hexagonal coordinate, based on "axial coordinates".  It can also enumerate
 * its neighbours, so that path-finding is enabled.
 *
 * See https://www.redblobgames.com/grids/hexagons/ for some basic discussion
 * of the underlying theory.
 */
class HexCoord
{

public:

  /** Integer type used to hold the coordinates.  */
  using IntT = int16_t;

  class Difference;

private:

  /** The "first" axial coordinate.  */
  IntT x;
  /** The "second" axial coordinate.  */
  IntT y;

  class NeighbourList;

  friend class std::hash<HexCoord>;

public:

  explicit constexpr HexCoord (const IntT xx, const IntT yy)
    : x(xx), y(yy)
  {}

  constexpr HexCoord ()
    : x(0), y(0)
  {}

  HexCoord (const HexCoord&) = default;
  HexCoord& operator= (const HexCoord&) = default;

  friend bool operator== (const HexCoord& a, const HexCoord& b);
  friend bool operator!= (const HexCoord& a, const HexCoord& b);
  friend bool operator< (const HexCoord& a, const HexCoord& b);

  friend std::ostream& operator<< (std::ostream& out, const HexCoord& c);

  constexpr IntT
  GetX () const
  {
    return x;
  }

  constexpr IntT
  GetY () const
  {
    return y;
  }

  void operator+= (const Difference& delta);
  friend constexpr Difference operator- (const HexCoord& a, const HexCoord& b);

  /**
   * Computes and returns the matching z coordinate in cubic hex coordinates.
   */
  constexpr IntT GetZ () const;

  /**
   * Returns true if it is a principal direction from the current instance
   * to the given target.  In this case, the direction itself and the number
   * of steps are filled in.
   */
  bool IsPrincipalDirectionTo (const HexCoord& target,
                               Difference& dir, IntT& steps) const;

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
 * Opaque class representing the difference of two coordinates, i.e.
 * a "direction" that can be added onto another coordinate.  Internally
 * it has the same functionality as a normal coordinate, but is a different
 * type to enable stronger typing in the allowed operations.
 */
class HexCoord::Difference
{

private:

  /** The "first" axial coordinate.  */
  IntT x;
  /** The "second" axial coordinate.  */
  IntT y;

  friend class HexCoord;

public:

  explicit constexpr Difference (const IntT xx, const IntT yy)
    : x(xx), y(yy)
  {}

  constexpr Difference ()
    : x(0), y(0)
  {}

  Difference (const Difference&) = default;
  Difference& operator= (const Difference&) = default;

  friend bool operator== (const Difference& a, const Difference& b);
  friend bool operator!= (const Difference& a, const Difference& b);

  friend constexpr Difference operator* (const IntT f, const Difference& d);
  friend constexpr HexCoord operator+ (const HexCoord& a, const Difference& b);

  /**
   * Rotates the coordinate clock-wise for n steps of 60 degrees around the
   * origin.  (These are the "natural" rotations on a hex grid.)
   */
  Difference RotateCW (int steps) const;

};

/**
 * Dummy class that gives access (and can be iterated over) to the neighbours
 * of a certain hex cell.
 */
class HexCoord::NeighbourList
{

private:

  /** The underlying hex cell.  */
  const HexCoord& centre;

  class ConstIterator;

public:

  explicit NeighbourList (const HexCoord& c)
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
class HexCoord::NeighbourList::ConstIterator
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

  explicit ConstIterator (const HexCoord& c, const bool end)
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

  size_t operator() (const pxd::HexCoord& c) const;

};

} // namespace std

#include "coord.tpp"

#endif // HEXAGONAL_COORD_HPP
