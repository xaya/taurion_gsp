/* Template implementation code for rangemap.hpp.  */

#include <glog/logging.h>

#include <algorithm>

namespace pxd
{

template <typename T>
  RangeMap<T>::RangeMap (const HexCoord& c, const HexCoord::IntT r,
                         const T& val)
  : centre(c), range(r), defaultValue(val),
    data(3 * range * (range + 1) + 1, defaultValue)
{
  /* The size of data can be computed as follows:

        2 sum i, i = range + 1 to 2 range (for all except the middle row)
        2 range + 1 (for the middle row)

     Using the well-known "n (n + 1) / 2" formula, the sum can be resolved,
     yielding the expression we use above.  */
}

template <typename T>
  int
  RangeMap<T>::GetIndex (const HexCoord& c) const
{
  if (HexCoord::DistanceL1 (c, centre) > range)
    return -1;

  const int row = range + c.GetY () - centre.GetY ();
  CHECK_GE (row, 0);
  CHECK_LT (row, 2 * range + 1);

  /* The x-difference for the start of the current row (with "column 0")
     starts at zero for the top row, then increases to range for the
     middle row, and then stays there.  */
  const int col = c.GetX () - centre.GetX () + std::min<int> (row, range);
  CHECK_GE (col, 0);
  CHECK_LT (col, 2 * range + 1);

  /* The difficulty now is to compute the offset into data at which the
     current row's storage starts.  For rows up to range + 1 (i.e. the one
     after the middle row), this is just the sum of preceding rows that
     are all increasing in length:

        sum (range + i), i = 1 to row
        -> row (row + 2 range + 1) / 2

     After that, we have to add decreasing row lengths to that result:

        sum (2 * range - i), i = 0 to (row - (range + 2))
        -> (5 range + 2 - row) (row - range - 1) / 2
  */

  const int cappedRow = std::min (row, range + 1);
  int rowOffs = cappedRow * (cappedRow + 2 * range + 1) / 2;

  if (row > cappedRow)
    rowOffs += (5 * range + 2 - row) * (row - range - 1) / 2;

  return rowOffs + col;
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
