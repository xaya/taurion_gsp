/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#include "target.hpp"

#include "character.hpp"
#include "dbtest.hpp"
#include "schema.hpp"

#include "hexagonal/coord.hpp"

#include <benchmark/benchmark.h>

#include <glog/logging.h>

namespace pxd
{
namespace
{

/**
 * Adds n test characters of the given faction at the given position.
 */
void
InsertTestCharacters (Database& db, const unsigned n,
                      const HexCoord& pos, const Faction f)
{
  CharacterTable tbl(db);
  for (unsigned i = 0; i < n; ++i)
    {
      auto h = tbl.CreateNew ("domob", f);
      h->SetPosition (pos);
    }
}

/**
 * Benchmarks target lookup for a given range (from one attacker).
 *
 * Arguments are:
 *  - Size of the range
 *  - Characters in range
 *  - Characters outside range but on same x coordinate
 *  - Characters outside range but on same y coordinate
 *  - Friendly characters on same position
 */
void
TargetFinding (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (*db);

  const HexCoord::IntT range = state.range (0);
  const unsigned inRange = state.range (1);
  const unsigned sameX = state.range (2);
  const unsigned sameY = state.range (3);
  const unsigned friendly = state.range (4);

  InsertTestCharacters (db, inRange, HexCoord (0, 0), Faction::GREEN);
  InsertTestCharacters (db, sameX, HexCoord (0, 2 * range), Faction::GREEN);
  InsertTestCharacters (db, sameY, HexCoord (2 * range, 0), Faction::GREEN);
  InsertTestCharacters (db, friendly, HexCoord (0, 0), Faction::RED);

  TargetFinder finder(db);
  for (auto _ : state)
    {
      const auto found = finder.FindInRange (HexCoord (0, 0), range,
                                             Faction::RED, true, false,
                                             proto::TargetId ());
      CHECK_EQ (found.size (), inRange);
    }
}
BENCHMARK (TargetFinding)
  ->Unit (benchmark::kMicrosecond)
  ->Args ({10, 1, 0, 0, 0})
  ->Args ({10, 100, 0, 0, 0})
  ->Args ({10, 10000, 0, 0, 0})
  ->Args ({100, 1, 0, 0, 0})
  ->Args ({100, 100, 0, 0, 0})
  ->Args ({100, 10000, 0, 0, 0})
  ->Args ({100, 100, 0, 0, 100})
  ->Args ({100, 100, 0, 0, 1000})
  ->Args ({10, 100, 10000, 0, 0})
  ->Args ({10, 100, 0, 10000, 0});

/**
 * Benchmarks the queries of one fighter next to a pile of enemies on one tile:
 * finding the five closest, picking from the closest and checking whether
 * one is still in range.  None of them should depend on the size of the pile.
 *
 * Arguments are:
 *  - Number of targets on the tile
 */
void
PileFinding (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (*db);

  const unsigned n = state.range (0);
  InsertTestCharacters (db, n, HexCoord (0, 0), Faction::GREEN);

  const HexCoord centre(0, 1);
  TargetFinder finder(db);
  for (auto _ : state)
    {
      const auto found = finder.FindInRange (centre, 10, Faction::RED,
                                             true, false, proto::TargetId (),
                                             5);
      CHECK_EQ (found.size (), 5);

      const auto closest = finder.FindClosest (centre, 10, Faction::RED,
                                               true, false, proto::TargetId ());
      CHECK_EQ (closest.size (), n);
      CHECK (finder.IsInRange (closest[n / 2], centre, 10, Faction::RED,
                               true, false));
    }
}
BENCHMARK (PileFinding)
  ->Unit (benchmark::kMicrosecond)
  ->Arg (10)
  ->Arg (1000)
  ->Arg (10000);

} // anonymous namespace
} // namespace pxd
