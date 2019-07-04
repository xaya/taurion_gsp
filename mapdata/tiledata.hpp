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

#ifndef MAPDATA_TILEDATA_HPP
#define MAPDATA_TILEDATA_HPP

#include <cstddef>
#include <cstdint>

namespace pxd
{
namespace tiledata
{

/** Minimum value of the axial y coordinate that is still on the map.  */
extern const int minY;
/** Maximum value of the axial y coordinate that is still on the map.  */
extern const int maxY;

/**
 * For given y in [0, maxY - minY], the minimum x coordinate for tiles
 * to be still on the map.
 */
extern const int minX[];
/** For given y, the maximum x coordinate for tiles to be still on the map.  */
extern const int maxX[];

/**
 * For given y, the index into a general data array with entries for each
 * tile where the row for that y starts.  This is used, for instance, for
 * the maps of dynamic obstacles.
 */
extern const size_t offsetForY[];

/** Total number of tiles.  */
extern const size_t numTiles;

namespace obstacles
{

/** For given y, the offset into bitData where the data for that row starts.  */
extern const size_t bitDataOffsetForY[];

/** Size of the raw bitvector data.  */
extern const size_t bitDataSize;

} // namespace obstacles

namespace regions
{

/** Number of bytes per encoded region ID.  */
constexpr int BYTES_PER_ID = 3;

/**
 * For given y, the offset into the "compact region data" where data for the
 * given row starts.  This is not measured in bytes but in "objects", i.e.
 * how many x coordinates or 24-bit IDs to skip from the beginning.
 */
extern const size_t compactOffsetForY[];

/** Number of entries for the compact region data arrays.  */
extern const size_t compactEntries;

} // namespace regions

} // namespace tiledata
} // namespace pxd

extern "C"
{

/* Bit vector data for the "passable" flag of all map tiles.  This holds the
   bytes for all rows after each other.  For each row, we have bytes that
   encode the passable flag in "little endian" bit-vector format, i.e. the
   first byte holds the flags for the first (least x coordinate) 8 tiles and
   so on.  Within each byte, the least-significant bit holds the flag for the
   tile with lowest x coordinate.  */
extern const unsigned char blob_obstacles_start;
extern const unsigned char blob_obstacles_end;

/* The array of int16_t's that encode the x coordinates for the compact
   storage of the region map.  */
extern const int16_t blob_region_xcoord_start;
extern const int16_t blob_region_xcoord_end;

/* The blob of raw data encoding the compact region IDs.  Each triplet of bytes
   encodes one 24-bit region ID.  */
extern const unsigned char blob_region_ids_start;
extern const unsigned char blob_region_ids_end;

}

#endif // MAPDATA_TILEDATA_HPP
