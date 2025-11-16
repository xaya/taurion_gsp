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
operator== (const HexCoord::Difference& a, const HexCoord::Difference& b)
{
  return a.x == b.x && a.y == b.y;
}

inline bool
operator!= (const HexCoord::Difference& a, const HexCoord::Difference& b)
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
HexCoord::operator+= (const HexCoord::Difference& delta)
{
  x += delta.x;
  y += delta.y;
}

inline constexpr HexCoord::Difference
operator* (const HexCoord::IntT f, const HexCoord::Difference& d)
{
  return HexCoord::Difference (f * d.x, f * d.y);
}

inline constexpr HexCoord
operator+ (const HexCoord& a, const HexCoord::Difference& b)
{
  return HexCoord (a.GetX () + b.x, a.GetY () + b.y);
}

inline constexpr HexCoord::Difference
operator- (const HexCoord& a, const HexCoord& b)
{
  return HexCoord::Difference (a.x - b.x, a.y - b.y);
}

inline constexpr HexCoord::IntT
HexCoord::GetZ () const
{
  return -x - y;
}

inline HexCoord::Difference
HexCoord::Difference::RotateCW (int steps) const
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
      return Difference (x + y, -x);

    case 2:
      return Difference (y, -(x + y));

    case 3:
      return Difference (-x, -y);

    case 4:
      return Difference (-(x + y), x);

    case 5:
      return Difference (-y, x + y);

    default:
      LOG (FATAL) << "Unexpected rotation steps: " << steps;
    }
}

inline bool
HexCoord::IsPrincipalDirectionTo (const HexCoord& target,
                                  Difference& dir, IntT& steps) const
{
  const auto diff = target - *this;
  steps = -1;

  if (diff.x == 0)
    steps = std::abs (diff.y);
  else if (diff.y == 0)
    steps = std::abs (diff.x);
  else if (diff.x + diff.y == 0)
    steps = std::abs (diff.x);

  if (steps == -1 || steps == 0)
    return false;

  CHECK_GT (steps, 0);
  dir.x = diff.x / steps;
  dir.y = diff.y / steps;
  return true;
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
  constexpr std::array<HexCoord::Difference, 6> dirs =
    {
      HexCoord::Difference (1, 0),
      HexCoord::Difference (-1, 0),
      HexCoord::Difference (0, 1),
      HexCoord::Difference (0, -1),
      HexCoord::Difference (1, -1),
      HexCoord::Difference (-1, 1),
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
