/* Template implementation code for rangemap.hpp.  */

#include <glog/logging.h>

namespace pxd
{

template <typename T>
  RangeMap<T>::RangeMap (const HexCoord& c, const HexCoord::IntT r,
                         const T& val)
  : centre(c), range(r), defaultValue(val)
{}

template <typename T>
  T&
  RangeMap<T>::Access (const HexCoord& c)
{
  CHECK_LE (HexCoord::DistanceL1 (c, centre), range)
      << "Out-of-range access: "
      << c << " is out of range " << range << " around " << centre;
  const auto mit = data.find (c);
  if (mit != data.end ())
    return mit->second;
  return data.emplace (c, defaultValue).first->second;
}

template <typename T>
  const T&
  RangeMap<T>::Get (const HexCoord& c) const
{
  if (HexCoord::DistanceL1 (c, centre) > range)
    return defaultValue;
  const auto mit = data.find (c);
  if (mit != data.end ())
    return mit->second;
  return defaultValue;
}

} // namespace pxd
