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
 * Writes an unsigned 24-bit integer in little endian format.
 */
void WriteInt24 (std::ostream& out, uint32_t val);

} // namespace pxd

#endif // MAPDATA_DATAIO_HPP
