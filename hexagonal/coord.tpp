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

#include <array>
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

inline constexpr HexCoord
operator* (const HexCoord::IntT f, const HexCoord& c)
{
  return HexCoord (f * c.GetX (), f * c.GetY ());
}

inline constexpr HexCoord
operator+ (const HexCoord& a, const HexCoord& b)
{
  return HexCoord (a.GetX () + b.GetX (), a.GetY () + b.GetY ());
}

inline constexpr HexCoord::IntT
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
  /* The six principal directions that we have.  The order here is important,
     as it e.g. also specifies how neighbours are enumerated and thus how
     the path finder orders paths of the same length.  */
  constexpr std::array<HexCoord, 6> dirs =
    {
      HexCoord (1, 0),
      HexCoord (-1, 0),
      HexCoord (0, 1),
      HexCoord (0, -1),
      HexCoord (1, -1),
      HexCoord (-1, 1),
    };

  if (next >= 0 && next < dirs.size ())
    return centre + dirs[next];

  if (next == 6)
    LOG (FATAL) << "Cannot dereference iterator at the end";

  LOG (FATAL) << "Unexpected value of 'next': " << next;
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
