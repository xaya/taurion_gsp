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

#include "dyntiles.hpp"

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
 * Benchmarks the construction of an empty DynTiles<bool> instance with
 * no further access.
 */
void
DynTilesBoolConstruction (benchmark::State& state)
{
  for (auto _ : state)
    {
      DynTiles<bool> dyn(false);
    }
}
BENCHMARK (DynTilesBoolConstruction)
  ->Unit (benchmark::kMicrosecond);

/**
 * Benchmarks updates in a DynTiles<bool> instance.  Takes one argument,
 * the number of random coordinates to update.  Each coordinate is changed
 * from the default false to true and then back to false in a later step.
 */
void
DynTilesBoolDoubleUpdate (benchmark::State& state)
{
  const unsigned n = state.range (0);
  std::srand (42);
  const auto coords = RandomCoords (n);

  for (auto _ : state)
    {
      state.PauseTiming ();
      DynTiles<bool> dyn(false);
      state.ResumeTiming ();

      for (const auto& c : coords)
        dyn.Access (c) = true;
      for (const auto& c : coords)
        dyn.Access (c) = false;
    }
}
BENCHMARK (DynTilesBoolDoubleUpdate)
  ->Unit (benchmark::kMillisecond)
  ->Arg (100000)
  ->Arg (1000000);

/**
 * Benchmarks reads of uninitialised (default) values from a DynTiles<bool>
 * instance.  Takes two arguments:  The number of random coordinates to look
 * at, and the number of reads for each of them.
 */
void
DynTilesBoolReadDefault (benchmark::State& state)
{
  const unsigned n = state.range (0);
  const unsigned rounds = state.range (1);

  std::srand (42);
  const auto coords = RandomCoords (n);

  for (auto _ : state)
    {
      state.PauseTiming ();
      DynTiles<bool> dyn(false);
      state.ResumeTiming ();

      for (unsigned t = 0; t < rounds; ++t)
        for (const auto& c : coords)
          CHECK (!dyn.Access (c));
    }
}
BENCHMARK (DynTilesBoolReadDefault)
  ->Unit (benchmark::kMillisecond)
  ->Args ({1000, 100})
  ->Args ({1000, 1000})
  ->Args ({10000, 100})
  ->Args ({10000, 1000});

/**
 * Benchmarks reads of initialised (non-default) values from a DynTiles<bool>
 * instance.  Takes two arguments:  The number of random coordinates to look
 * at, and the number of reads for each of them.
 */
void
DynTilesBoolReadInitialised (benchmark::State& state)
{
  const unsigned n = state.range (0);
  const unsigned rounds = state.range (1);

  std::srand (42);
  const auto coords = RandomCoords (n);

  for (auto _ : state)
    {
      state.PauseTiming ();
      DynTiles<bool> dyn(false);
      for (const auto& c : coords)
        dyn.Access (c) = true;
      state.ResumeTiming ();

      for (unsigned t = 0; t < rounds; ++t)
        for (const auto& c : coords)
          CHECK (dyn.Access (c));
    }
}
BENCHMARK (DynTilesBoolReadInitialised)
  ->Unit (benchmark::kMillisecond)
  ->Args ({1000, 100})
  ->Args ({1000, 1000})
  ->Args ({10000, 100})
  ->Args ({10000, 1000});

} // anonymous namespace
} // namespace pxd
