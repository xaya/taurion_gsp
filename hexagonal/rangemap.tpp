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

/* Template implementation code for rangemap.hpp.  */

#include <glog/logging.h>

#include <cmath>

namespace pxd
{

template <typename T>
  RangeMap<T>::RangeMap (const HexCoord& c, const HexCoord::IntT r,
                         const T& val)
  : centre(c), range(r), defaultValue(val),
    data(std::pow (2 * range + 1, 2), defaultValue)
{}

template <typename T>
  inline int
  RangeMap<T>::GetIndex (const HexCoord& c) const
{
  if (HexCoord::DistanceL1 (c, centre) > range)
    return -1;

  const int row = range + c.GetX () - centre.GetX ();
  CHECK_GE (row, 0);
  CHECK_LT (row, 2 * range + 1);

  const int col = range + c.GetY () - centre.GetY ();
  CHECK_GE (col, 0);
  CHECK_LT (col, 2 * range + 1);

  return row + col * (2 * range + 1);
}

template <typename T>
  inline typename std::vector<T>::reference
  RangeMap<T>::Access (const HexCoord& c)
{
  const int ind = GetIndex (c);
  CHECK_GE (ind, 0)
      << "Out-of-range access: "
      << c << " is out of range " << range << " around " << centre;
  CHECK_LT (ind, data.size ());
  return data[ind];
}

template <typename T>
  inline typename std::vector<T>::const_reference
  RangeMap<T>::Get (const HexCoord& c) const
{
  const int ind = GetIndex (c);
  if (ind == -1)
    return defaultValue;
  CHECK_GE (ind, 0);
  CHECK_LT (ind, data.size ());
  return data[ind];
}

} // namespace pxd
