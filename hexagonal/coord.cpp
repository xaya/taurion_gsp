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

#include "coord.hpp"

#include <glog/logging.h>

#include <cmath>

namespace pxd
{

HexCoord::NeighbourList
HexCoord::Neighbours () const
{
  return NeighbourList (*this);
}

HexCoord::NeighbourList::ConstIterator
HexCoord::NeighbourList::begin () const
{
  return ConstIterator (centre, false);
}

HexCoord::NeighbourList::ConstIterator
HexCoord::NeighbourList::end () const
{
  return ConstIterator (centre, true);
}

void
HexCoord::NeighbourList::ConstIterator::operator++ ()
{
  CHECK_LT (next, 6) << "Cannot advance iterator already at the end";
  ++next;
}

HexCoord
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
