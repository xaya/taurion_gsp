#include "regionmap.hpp"

#include "tiledata.hpp"

#include "hexagonal/rangemap.hpp"

#include <glog/logging.h>

#include <algorithm>
#include <queue>

namespace pxd
{

constexpr RegionMap::IdT RegionMap::OUT_OF_MAP;

RegionMap::RegionMap ()
{
  using tiledata::regions::compactEntries;
  CHECK_EQ (&blob_region_xcoord_end - &blob_region_xcoord_start,
            compactEntries);
  CHECK_EQ (&blob_region_ids_end - &blob_region_ids_start,
            tiledata::regions::BYTES_PER_ID * compactEntries);
}

RegionMap::IdT
RegionMap::GetRegionId (const HexCoord& c) const
{
  const auto x = c.GetX ();
  const auto y = c.GetY ();

  if (y < tiledata::minY || y > tiledata::maxY)
    return OUT_OF_MAP;
  const int yInd = y - tiledata::minY;

  if (x < tiledata::minX[yInd] || x > tiledata::maxX[yInd])
    return OUT_OF_MAP;

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

  CHECK_NE (res, OUT_OF_MAP);
  return res;
}

namespace
{

/**
 * Simple helper class for the state while we do a flood fill of some region.
 */
class RegionFiller
{

private:

  /** The result set being built up.  */
  std::set<HexCoord> regionTiles;

  /** Stores whether or not a tile has already been processed.  */
  RangeMap<bool> processed;

  /**
   * The queue of tiles to still process.  Each tile on here is known to be
   * in the region and already in the result set, but we still need to
   * iterate over and process its neighbours.
   */
  std::queue<HexCoord> todo;

public:

  /**
   * Constructs the object with basic initialisation.  The initial tile
   * ("seed") for the flood fill has not yet been added here.  The l1range
   * of the processed map is chosen such that all regions on the actual
   * map fit inside.
   */
  explicit RegionFiller (const HexCoord& c)
    : processed(c, 100, false)
  {}

  /**
   * Moves out the result set.
   */
  std::set<HexCoord>
  MoveResult ()
  {
    return std::move (regionTiles);
  }

  /**
   * Adds a tile to the result set and queues it for processing.  This is
   * called whenever we determine that some coordinate is in the region.
   */
  void
  AddRegionTile (const HexCoord& c)
  {
    /* Since the RangeMap has an underlying data storage of std::vector<bool>,
       its Access method does not return bool& but the bit-reference wrapper.
       Hence we cannot say "auto&" here, but in effect p acts still as a
       reference to the value.  */
    auto p = processed.Access (c);
    if (p)
      return;
    p = true;

    const auto insertRes = regionTiles.insert (c);
    CHECK (insertRes.second);

    todo.push (c);
  }

  /**
   * Extracts the next tile for which we need to process its neighbours.
   * Returns false if there is none (i.e. we are done).
   */
  bool
  NextTodo (HexCoord& c)
  {
    if (todo.empty ())
      return false;

    c = todo.front ();
    todo.pop ();
    return true;
  }

  /**
   * Checks whether or not the given tile has already been processed.
   */
  bool
  AlreadyProcessed (const HexCoord& c) const
  {
    return processed.Get (c);
  }

};

} // anonymous namespace

std::set<HexCoord>
RegionMap::GetRegionShape (const HexCoord& c, IdT& id) const
{
  id = GetRegionId (c);
  CHECK_NE (id, OUT_OF_MAP) << "Coordinate is out of the map: " << c;

  RegionFiller filler(c);
  filler.AddRegionTile (c);

  HexCoord todo;
  while (filler.NextTodo (todo))
    for (const auto& n : todo.Neighbours ())
      {
        /* AddRegionTile ignores already processed tiles as well, but by doing
           so here we save a somewhat expensive call to GetRegionInternal.  */
        if (filler.AlreadyProcessed (n))
          continue;

        if (GetRegionId (n) == id)
          filler.AddRegionTile (n);
      }

  return filler.MoveResult ();
}

} // namespace pxd
