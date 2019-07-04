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
