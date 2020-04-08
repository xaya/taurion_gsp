/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

/* Template implementation code for coord.hpp.  */

#include <glog/logging.h>

#include <cmath>
#include <limits>

namespace pxd
{

inline bool
operator== (const HexCoord& a, const HexCoord& b)
{
  return a.x == b.x && a.y == b.y;
}

inline bool
operator!= (const HexCoord& a, const HexCoord& b)
{
  return !(a == b);
}

inline bool
operator< (const HexCoord& a, const HexCoord& b)
{
  if (a.x != b.x)
    return a.x < b.x;
  return a.y < b.y;
}

inline std::ostream&
operator<< (std::ostream& out, const HexCoord& c)
{
  out << "(" << c.x << ", " << c.y << ")";
  return out;
}

inline void
HexCoord::operator+= (const HexCoord& delta)
{
  x += delta.GetX ();
  y += delta.GetY ();
}

inline HexCoord
operator* (const HexCoord::IntT f, const HexCoord& c)
{
  return HexCoord (f * c.GetX (), f * c.GetY ());
}

inline HexCoord
operator+ (const HexCoord& a, const HexCoord& b)
{
  return HexCoord (a.GetX () + b.GetX (), a.GetY () + b.GetY ());
}

inline HexCoord::IntT
HexCoord::GetZ () const
{
  return -x - y;
}

inline HexCoord
HexCoord::RotateCW (int steps) const
{
  steps %= 6;
  if (steps < 0)
    {
      steps += 6;
      steps %= 6;
    }

  switch (steps)
    {
    case 0:
      return *this;

    case 1:
      return HexCoord (-GetZ (), -GetX ());

    case 2:
      return HexCoord (GetY (), GetZ ());

    case 3:
      return HexCoord (-GetX (), -GetY ());

    case 4:
      return HexCoord (GetZ (), GetX ());

    case 5:
      return HexCoord (-GetY (), -GetZ ());

    default:
      LOG (FATAL) << "Unexpected rotation steps: " << steps;
    }
}

inline HexCoord::IntT
HexCoord::DistanceL1 (const HexCoord& a, const HexCoord& b)
{
  const IntT twice = std::abs (a.x - b.x)
                      + std::abs (a.y - b.y)
                      + std::abs (a.GetZ () - b.GetZ ());
  CHECK (twice % 2 == 0);
  return twice >> 1;
}

inline HexCoord::NeighbourList
HexCoord::Neighbours () const
{
  return NeighbourList (*this);
}

inline HexCoord::NeighbourList::ConstIterator
HexCoord::NeighbourList::begin () const
{
  return ConstIterator (centre, false);
}

inline HexCoord::NeighbourList::ConstIterator
HexCoord::NeighbourList::end () const
{
  return ConstIterator (centre, true);
}

inline void
HexCoord::NeighbourList::ConstIterator::operator++ ()
{
  CHECK_LT (next, 6) << "Cannot advance iterator already at the end";
  ++next;
}

inline HexCoord
HexCoord::NeighbourList::ConstIterator::operator* () const
{
  /* In cubic coordinates, we find the neighbours by picking two out of the
     three coordinates and then incrementing one and decrementing the other
     (for a total of six potential choices / neighbours).

     So in axial coordinates, we can pick x/y as the first and z as the second,
     in which case we simply increment/decrement x or y and are done (as z is
     implicit and thus does not need to be changed.

     Or we can pick x and y as the two, in which case we have to increment
     one and decrement the other explicitly.  */

  switch (next)
    {
    /* Choice is x and z.  */
    case 0:
      return HexCoord (centre.x + 1, centre.y);
    case 1:
      return HexCoord (centre.x - 1, centre.y);

    /* Choice is y and z.  */
    case 2:
      return HexCoord (centre.x, centre.y + 1);
    case 3:
      return HexCoord (centre.x, centre.y - 1);

    /* Choice is x and y.  */
    case 4:
      return HexCoord (centre.x + 1, centre.y - 1);
    case 5:
      return HexCoord (centre.x - 1, centre.y + 1);

    /* Fail for invalid states of the iterator.  */
    case 6:
      LOG (FATAL) << "Cannot dereference iterator at the end";
    default:
      LOG (FATAL) << "Unexpected value of 'next': " << next;
    }
}

} // namespace pxd

namespace std
{

inline size_t
hash<pxd::HexCoord>::operator() (const pxd::HexCoord& c) const
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

} // namespace std
