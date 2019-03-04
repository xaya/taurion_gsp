#ifndef MAPDATA_TILEDATA_HPP
#define MAPDATA_TILEDATA_HPP

#include <cstddef>

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
 * For given y, the offset into the map data blob for where the region IDs
 * (encoded in three bytes each) for that row start.
 */
extern const size_t regionIdOffsetForY[];

/** Number of bytes in the raw region map data.  */
extern const size_t regionMapSize;

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

}

#endif // MAPDATA_TILEDATA_HPP
