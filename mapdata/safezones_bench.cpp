/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "safezones.hpp"

#include "benchutils.hpp"

#include "hexagonal/coord.hpp"
#include "proto/roconfig.hpp"

#include <benchmark/benchmark.h>

#include <cstdlib>

namespace pxd
{
namespace
{

/**
 * Benchmarks construction of the SafeZones instance for the different types
 * of RoConfig's we have.
 */
void
SafeZonesConstructor (benchmark::State& state, const xaya::Chain chain)
{
  const RoConfig cfg(chain);
  for (auto _ : state)
    {
      SafeZones sz(cfg);
      /* Just the constructor alone does the cache construction and is
         what we want to benchmark.  */
    }
}
BENCHMARK_CAPTURE (SafeZonesConstructor, main, xaya::Chain::MAIN)
  ->Unit (benchmark::kMillisecond);
BENCHMARK_CAPTURE (SafeZonesConstructor, test, xaya::Chain::TEST)
  ->Unit (benchmark::kMillisecond);
BENCHMARK_CAPTURE (SafeZonesConstructor, regtest, xaya::Chain::REGTEST)
  ->Unit (benchmark::kMillisecond);

/**
 * Benchmarks the IsNoCombat access.  Accepts one argument, which is the number
 * of coordinates to check in one benchmark run.
 */
void
SafeZonesIsNoCombat (benchmark::State& state)
{
  const RoConfig cfg(xaya::Chain::MAIN);
  const SafeZones sz(cfg);
  const size_t n = state.range (0);

  std::srand (42);
  for (auto _ : state)
    {
      state.PauseTiming ();
      const auto coords = RandomCoords (n);
      state.ResumeTiming ();
      for (const auto& c : coords)
        sz.IsNoCombat (c);
    }
}
BENCHMARK (SafeZonesIsNoCombat)
  ->Unit (benchmark::kMicrosecond)
  ->Arg (1'000)
  ->Arg (1'000'000);

/**
 * Benchmarks the StarterFor access.  Accepts one argument, which is the number
 * of coordinates to check in one benchmark run.
 */
void
SafeZonesStarterFor (benchmark::State& state)
{
  const RoConfig cfg(xaya::Chain::MAIN);
  const SafeZones sz(cfg);
  const size_t n = state.range (0);

  std::srand (42);
  for (auto _ : state)
    {
      state.PauseTiming ();
      const auto coords = RandomCoords (n);
      state.ResumeTiming ();
      for (const auto& c : coords)
        sz.StarterFor (c);
    }
}
BENCHMARK (SafeZonesStarterFor)
  ->Unit (benchmark::kMicrosecond)
  ->Arg (1'000)
  ->Arg (1'000'000);

} // anonymous namespace
} // namespace pxd
