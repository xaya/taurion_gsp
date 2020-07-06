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

#include "ring.hpp"

#include <glog/logging.h>

#include <array>

namespace pxd
{

L1Ring::L1Ring (const HexCoord& c, const HexCoord::IntT r)
  : centre(c), radius(r)
{
  CHECK_GE (radius, 0);
}

L1Ring::ConstIterator
L1Ring::begin () const
{
  return ConstIterator (*this, false);
}

L1Ring::ConstIterator
L1Ring::end () const
{
  return ConstIterator (*this, true);
}

namespace
{

/**
 * Direction from the centre of a ring where the starting point of the
 * iteration is located.
 */
constexpr HexCoord RING_START_DIRECTION(1, 0);

/**
 * The direction vectors of the six sides along which we iterate in order.
 */
constexpr std::array<HexCoord, 6> RING_SIDE_DIRECTIONS =
  {
    HexCoord (0, -1),
    HexCoord (-1, 0),
    HexCoord (-1, 1),
    HexCoord (0, 1),
    HexCoord (1, 0),
    HexCoord (1, -1),
  };

} // anonymous namespace

L1Ring::ConstIterator::ConstIterator (const L1Ring& r, const bool end)
  : radius(r.radius), nextCornerIn(radius), side(0)
{
  CHECK_GE (radius, 0);

  cur = r.centre;
  cur += radius * RING_START_DIRECTION;

  if (end)
    side = RING_SIDE_DIRECTIONS.size ();
}

void
L1Ring::ConstIterator::operator++ ()
{
  CHECK_LT (side, RING_SIDE_DIRECTIONS.size ());

  /* Special case for radius zero:  Just always get to end.  */
  if (radius == 0)
    {
      side = RING_SIDE_DIRECTIONS.size ();
      return;
    }

  CHECK_GT (nextCornerIn, 0);

  cur += RING_SIDE_DIRECTIONS[side];
  --nextCornerIn;

  if (nextCornerIn == 0)
    {
      ++side;
      nextCornerIn = radius;
    }
}

const HexCoord&
L1Ring::ConstIterator::operator* () const
{
  CHECK_LT (side, RING_SIDE_DIRECTIONS.size ());
  return cur;
}

} // namespace pxd
