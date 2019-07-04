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

/* Template implementation code for dyntiles.hpp.  */

#include "tiledata.hpp"

#include <glog/logging.h>

namespace pxd
{

template <typename T>
  DynTiles<T>::DynTiles (const T& val)
  : data(tiledata::numTiles, val)
{}

template <typename T>
  size_t
  DynTiles<T>::GetIndex (const HexCoord& c)
{
  const auto x = c.GetX ();
  const auto y = c.GetY ();

  using namespace tiledata;

  CHECK (y >= minY && y <= maxY);
  const auto yInd = y - minY;

  CHECK (x >= minX[yInd] && x <= maxX[yInd]);
  return offsetForY[yInd] + x - minX[yInd];
}

template <typename T>
  typename std::vector<T>::reference
  DynTiles<T>::Access (const HexCoord& c)
{
  return data[GetIndex (c)];
}

template <typename T>
  typename std::vector<T>::const_reference
  DynTiles<T>::Get (const HexCoord& c) const
{
  return data[GetIndex (c)];
}

} // namespace pxd
