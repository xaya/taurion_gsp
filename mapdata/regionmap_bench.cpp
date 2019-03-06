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
 * Constructs a vector of n "random" coordinates.  It seeds the RNG
 * deterministically, so that each time the same sequence is returned for
 * a given n (and thus it can be used to compare different RangeMap
 * implementations in a fair way).
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
 * Runs a benchmark of finding the region IDs of n random coordinates
 * using the given RegionMap.
 */
void
BenchmarkRegionMap (benchmark::State& state,
                   const RegionMap& rm, const unsigned n)
{
  const std::vector<HexCoord> coords = RandomCoords (n);

  for (auto _ : state)
    for (const auto& c : coords)
      rm.GetRegionForTile (c);
}

void
InMemoryRegionMap (benchmark::State& state)
{
  auto rm = NewInMemoryRegionMap ("regionmap.bin");
  BenchmarkRegionMap (state, *rm, state.range (0));
}
BENCHMARK (InMemoryRegionMap)
  ->Unit (benchmark::kMillisecond)
  ->Arg (1000)
  ->Arg (100000)
  ->Arg (1000000);

void
StreamRegionMap (benchmark::State& state)
{
  auto rm = NewStreamRegionMap ("regionmap.bin");
  BenchmarkRegionMap (state, *rm, state.range (0));
}
BENCHMARK (StreamRegionMap)
  ->Unit (benchmark::kMillisecond)
  ->Arg (1000)
  ->Arg (100000)
  ->Arg (1000000);

void
MemMappedRegionMap (benchmark::State& state)
{
  auto rm = NewMemMappedRegionMap ("regionmap.bin");
  BenchmarkRegionMap (state, *rm, state.range (0));
}
BENCHMARK (MemMappedRegionMap)
  ->Unit (benchmark::kMillisecond)
  ->Arg (1000)
  ->Arg (100000)
  ->Arg (1000000);

void
CompactRegionMap (benchmark::State& state)
{
  auto rm = NewCompactRegionMap ();
  BenchmarkRegionMap (state, *rm, state.range (0));
}
BENCHMARK (CompactRegionMap)
  ->Unit (benchmark::kMillisecond)
  ->Arg (1000)
  ->Arg (100000)
  ->Arg (1000000);

} // anonymous namespace
} // namespace pxd
