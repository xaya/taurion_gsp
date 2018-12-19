#ifndef MAPDATA_DATAIO_HPP
#define MAPDATA_DATAIO_HPP

#include <cstdint>
#include <iostream>

namespace pxd
{

/**
 * Reads a signed 16-bit integer in little endian format.
 */
int16_t ReadInt16 (std::istream& in);

/**
 * Writes a signed 16-bit integer in little endian format.
 */
void WriteInt16 (std::ostream& out, const int16_t val);

} // namespace pxd

#endif // MAPDATA_DATAIO_HPP
