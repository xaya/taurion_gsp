#include "regionmap.hpp"

#include "tiledata.hpp"

#include "hexagonal/coord.hpp"

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <vector>

namespace pxd
{
namespace
{

/**
 * Constructs a vector of n "random" coordinates.  It seeds the RNG
 * deterministically, so that each time the same sequence is returned for
 * a given n (so that it gives consistent results for comparisons).
 */
std::vector<HexCoord>
RandomCoords (const unsigned n)
{
  std::vector<HexCoord> res;
  res.reserve (n);

  std::srand (n);
  for (unsigned i = 0; i < n; ++i)
    {
      using namespace tiledata;

      const int y = minY + std::rand () % (maxY - minY + 1);
      const int yInd = y - minY;
      const int x = minX[yInd] + std::rand () % (maxX[yInd] - minX[yInd] + 1);

      res.emplace_back (x, y);
    }

  return res;
}

/**
 * Benchmarks the lookup of the region ID from a coordinate.  Accepts one
 * argument, the number of (random) tiles to look up.
 */
void
GetRegionForTile (benchmark::State& state)
{
  RegionMap rm;
  const std::vector<HexCoord> coords = RandomCoords (state.range (0));

  for (auto _ : state)
    for (const auto& c : coords)
      rm.GetRegionForTile (c);
}
BENCHMARK (GetRegionForTile)
  ->Unit (benchmark::kMillisecond)
  ->Arg (1000)
  ->Arg (10000)
  ->Arg (100000)
  ->Arg (1000000);

} // anonymous namespace
} // namespace pxd
