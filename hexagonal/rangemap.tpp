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
  : centre(c), range(r),
    data(std::pow (2 * range + 1, 2), val)
{}

template <typename T>
  inline bool
  RangeMap<T>::IsInRange (const HexCoord& c) const
{
  return HexCoord::DistanceL1 (c, centre) <= range;
}

template <typename T>
  inline int
  RangeMap<T>::GetIndex (const HexCoord& c) const
{
#ifdef ENABLE_SLOW_ASSERTS
  CHECK (IsInRange (c))
      << "Out-of-range access: "
      << c << " is out of range " << range << " around " << centre;
#endif // ENABLE_SLOW_ASSERTS

  const int row = range + c.GetX () - centre.GetX ();
  const int col = range + c.GetY () - centre.GetY ();

#ifdef ENABLE_SLOW_ASSERTS
  CHECK_GE (row, 0);
  CHECK_LT (row, 2 * range + 1);

  CHECK_GE (col, 0);
  CHECK_LT (col, 2 * range + 1);
#endif // ENABLE_SLOW_ASSERTS

  return row + col * (2 * range + 1);
}

template <typename T>
  inline typename std::vector<T>::reference
  RangeMap<T>::Access (const HexCoord& c)
{
  const int ind = GetIndex (c);

#ifdef ENABLE_SLOW_ASSERTS
  CHECK_GE (ind, 0);
  CHECK_LT (ind, data.size ());
#endif // ENABLE_SLOW_ASSERTS

  return data[ind];
}

template <typename T>
  inline typename std::vector<T>::const_reference
  RangeMap<T>::Get (const HexCoord& c) const
{
  const int ind = GetIndex (c);

#ifdef ENABLE_SLOW_ASSERTS
  CHECK_GE (ind, 0);
  CHECK_LT (ind, data.size ());
#endif // ENABLE_SLOW_ASSERTS

  return data[ind];
}

} // namespace pxd
