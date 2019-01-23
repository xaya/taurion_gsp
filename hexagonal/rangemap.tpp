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
  int
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
  T&
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
  const T&
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
