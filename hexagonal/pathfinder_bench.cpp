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
