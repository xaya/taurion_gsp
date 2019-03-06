#include "regionmap.hpp"

#include "tiledata.hpp"

#include <glog/logging.h>

#include <algorithm>

namespace pxd
{

RegionMap::RegionMap ()
{
  using tiledata::regions::compactEntries;
  CHECK_EQ (&blob_region_xcoord_end - &blob_region_xcoord_start,
            compactEntries);
  CHECK_EQ (&blob_region_ids_end - &blob_region_ids_start,
            tiledata::regions::BYTES_PER_ID * compactEntries);
}

RegionMap::IdT
RegionMap::GetRegionForTile (const HexCoord& c) const
{
  const auto x = c.GetX ();
  const auto y = c.GetY ();

  CHECK_GE (y, tiledata::minY);
  CHECK_LE (y, tiledata::maxY);
  const int yInd = y - tiledata::minY;

  CHECK_GE (x, tiledata::minX[yInd]);
  CHECK_LE (x, tiledata::maxX[yInd]);

  using tiledata::regions::compactOffsetForY;
  const int16_t* xBegin = &blob_region_xcoord_start + compactOffsetForY[yInd];
  const int16_t* xEnd;
  if (y < tiledata::maxY)
    xEnd = &blob_region_xcoord_start + compactOffsetForY[yInd + 1];
  else
    xEnd = &blob_region_xcoord_end;

  /* Calling std::upper_bound on the sorted row of x coordinates gives us
     the first element that is larger than our x.  This means that the
     entry we are looking for is the one just before it, as the largest
     that is less-or-equal to x.  Note that *xBegin is guaranteed to be the
     minimum x value, so that we are guaranteed to not get xBegin itself.  */
  const auto* xFound = std::upper_bound (xBegin, xEnd, x);
  CHECK_EQ (*xBegin, tiledata::minX[yInd]);
  CHECK_GT (xFound, xBegin);
  --xFound;
  CHECK_LE (*xFound, x);

  using tiledata::regions::BYTES_PER_ID;
  const size_t offs = xFound - &blob_region_xcoord_start;
  const unsigned char* data = &blob_region_ids_start + BYTES_PER_ID * offs;

  IdT res = 0;
  for (int i = 0; i < BYTES_PER_ID; ++i)
    res |= (static_cast<IdT> (data[i]) << (8 * i));

  return res;
}

} // namespace pxd
