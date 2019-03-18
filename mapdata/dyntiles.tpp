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
