#include "pathfinder.hpp"

#include "coord.hpp"

#include <benchmark/benchmark.h>

#include <glog/logging.h>

namespace pxd
{
namespace
{

PathFinder::DistanceT
EdgeWeights (const HexCoord& from, const HexCoord& to)
{
  return 1;
}

/**
 * Benchmarks the path finding algorithm on a hex map without any obstacles
 * (corresponding to the worst case).  One iteration corresponds to finding
 * the path to a target N tiles away, where N is the argument of the test.
 */
void
PathToTarget (benchmark::State& state)
{
  const HexCoord::IntT n = state.range (0);

  const HexCoord source(0, 0);
  const HexCoord target(n, 0);

  for (auto _ : state)
    {
      PathFinder finder(&EdgeWeights, target);
      const auto dist = finder.Compute (source, n);
      CHECK_EQ (dist, n);
    }
}
BENCHMARK (PathToTarget)
  ->Unit (benchmark::kMillisecond)
  ->RangeMultiplier (10)
  ->Range (1, 100);

/**
 * Benchmarks stepping of an already computed path.
 */
void
PathStepping (benchmark::State& state)
{
  const HexCoord::IntT n = state.range (0);

  const HexCoord source(0, 0);
  const HexCoord target(n, 0);

  PathFinder finder(&EdgeWeights, target);
  const auto dist = finder.Compute (source, n);
  CHECK_EQ (dist, n);

  for (auto _ : state)
    {
      auto stepper = finder.StepPath (source);
      while (stepper.HasMore ())
        stepper.Next ();
    }
}
BENCHMARK (PathStepping)
  ->Unit (benchmark::kMicrosecond)
  ->RangeMultiplier (10)
  ->Range (1, 100);

} // anonymous namespace
} // namespace pxd
