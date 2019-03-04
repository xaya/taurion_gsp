#ifndef MAPDATA_DATAIO_HPP
#define MAPDATA_DATAIO_HPP

#include <cstdint>
#include <iostream>

namespace pxd
{

/**
 * Reads an integer type in little endian format.
 */
template <typename T>
  T Read (std::istream& in);

/**
 * Writes an integer type in little endian format.
 */
template <typename T>
  void Write (std::ostream& out, const T val);

} // namespace pxd

#endif // MAPDATA_DATAIO_HPP
