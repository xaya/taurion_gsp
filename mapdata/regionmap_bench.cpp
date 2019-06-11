#include "regionmap.hpp"

#include "tiledata.hpp"

#include "hexagonal/coord.hpp"

#include <benchmark/benchmark.h>

#include <glog/logging.h>

#include <cstdlib>
#include <vector>

namespace pxd
{
namespace
{

/**
 * Returns a hex coordinate on the map chosen (mostly) randomly.
 */
HexCoord
RandomCoord ()
{
  using namespace tiledata;

  const int y = minY + std::rand () % (maxY - minY + 1);
  const int yInd = y - minY;
  const int x = minX[yInd] + std::rand () % (maxX[yInd] - minX[yInd] + 1);

  return HexCoord (x, y);
}

/**
 * Constructs a vector of n "random" coordinates.
 */
std::vector<HexCoord>
RandomCoords (const unsigned n)
{
  std::vector<HexCoord> res;
  res.reserve (n);

  for (unsigned i = 0; i < n; ++i)
    res.push_back (RandomCoord ());

  return res;
}

/**
 * Benchmarks the lookup of the region ID from a coordinate.  Accepts one
 * argument, the number of (random) tiles to look up.
 */
void
GetRegionId (benchmark::State& state)
{
  RegionMap rm;
  const unsigned n = state.range (0);

  std::srand (42);
  for (auto _ : state)
    {
      state.PauseTiming ();
      const auto coords = RandomCoords (n);
      state.ResumeTiming ();
      for (const auto& c : coords)
        rm.GetRegionId (c);
    }
}
BENCHMARK (GetRegionId)
  ->Unit (benchmark::kMillisecond)
  ->Arg (1000)
  ->Arg (1000000);

/**
 * Benchmarks computing the shape of a region (finding all tiles in it).
 */
void
GetRegionShape (benchmark::State& state)
{
  RegionMap rm;

  std::srand (42);
  for (auto _ : state)
    {
      state.PauseTiming ();
      const HexCoord c = RandomCoord ();
      state.ResumeTiming ();

      RegionMap::IdT id;
      const std::set<HexCoord> tiles = rm.GetRegionShape (c, id);

      /* Some basic checks on the data, just to make sure it makes
         sense very roughly.  */
      state.PauseTiming ();
      LOG_FIRST_N (INFO, 10) << "Region size: " << tiles.size ();
      CHECK_EQ (tiles.count (c), 1);
      CHECK_GT (tiles.size (), 10)
          << "Region " << id << " has only " << tiles.size () << " tiles";
      state.ResumeTiming ();
    }
}
BENCHMARK (GetRegionShape)
  ->Unit (benchmark::kMicrosecond);

} // anonymous namespace
} // namespace pxd
