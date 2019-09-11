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

/* Template implementation code for coord.hpp.  */

#include <glog/logging.h>

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

inline HexCoord::IntT
HexCoord::GetZ () const
{
  return -x - y;
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
