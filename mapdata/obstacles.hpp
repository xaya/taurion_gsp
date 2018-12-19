#ifndef MAPDATA_OBSTACLES_HPP
#define MAPDATA_OBSTACLES_HPP

namespace pxd
{
namespace obstacles
{

/** Minimum value of the axial y coordinate that is still on the map.  */
extern const int minY;
/** Maximum value of the axial y coordinate that is still on the map.  */
extern const int maxY;

/**
 * For given y in [0, (maxY - minY + 1)], the minimum x coordinate for tiles
 * to be still on the map.
 */
extern const int minX[];
/** For given y, the maximum x coordinate for tiles to be still on the map.  */
extern const int maxX[];

/** For given y, the offset into bitData where the data for that row starts.  */
extern const int bitDataOffsetForY[];

/**
 * Bit vector data for the "passable" flag of all map tiles.  This holds the
 * bytes for all rows after each other.  For each row, we have bytes that
 * encode the passable flag in "little endian" bit-vector format, i.e. the
 * first byte holds the flags for the first (least x coordinate) 8 tiles and
 * so on.  Within each byte, the least-significant bit holds the flag for the
 * tile with lowest x coordinate.
 */
extern const unsigned char bitData[];

} // namespace obstacles
} // namespace pxd

#endif // MAPDATA_OBSTACLES_HPP
