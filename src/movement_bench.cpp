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

#include "movement.hpp"

#include "protoutils.hpp"
#include "testutils.hpp"

#include "database/account.hpp"
#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
#include "database/schema.hpp"
#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <vector>

namespace pxd
{
namespace
{

/**
 * The speed of characters used in the benchmark.  We want to move one
 * tile per block.
 */
constexpr PathFinder::DistanceT SPEED = 1'000;

/**
 * Initialises the test account in the database.
 */
void
InitialiseAccount (Database& db)
{
  AccountsTable tbl(db);
  tbl.CreateNew ("domob")->SetFaction (Faction::RED);
}

/**
 * Constructs a test character in the given character table.  This takes
 * care of all necessary setup (e.g. the speed field).
 */
CharacterTable::Handle
CreateCharacter (CharacterTable& tbl)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->MutableProto ().set_speed (SPEED);
  return c;
}

/**
 * Benchmarks moving a single character a long way.  This gives us an idea of
 * how costly long-haul travel is in general.
 *
 * The total distance travelled is passed as argument.
 */
void
MovementLongHaul (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (*db);

  ContextForTesting ctx;

  const HexCoord::IntT dist = state.range (0);

  InitialiseAccount (db);

  CharacterTable tbl(db);
  const auto id = CreateCharacter (tbl)->GetId ();

  /* We start travelling from a non-zero origin.  The coordinate is chosen
     such that movement from it in positive x direction is free for a long
     enough path.  Updated for current splatmap obstacle data.  */
  const HexCoord origin(-100, -2'440);

  for (auto _ : state)
    {
      state.PauseTiming ();
      {
        const auto h = tbl.GetById (id);
        StopCharacter (*h);
        h->SetPosition (origin);
        auto* mv = h->MutableProto ().mutable_movement ();
        auto* wp = mv->mutable_waypoints ();
        *wp->Add () = CoordToProto (origin + HexCoord (dist, 0));
      }
      DynObstacles dyn(db, ctx);
      state.ResumeTiming ();

      do
        {
          ProcessAllMovement (db, dyn, ctx);
        }
      while (tbl.GetById (id)->GetProto ().has_movement ());

      state.PauseTiming ();
      CHECK_EQ (tbl.GetById (id)->GetPosition (), HexCoord (dist, 0) + origin);
      state.ResumeTiming ();
    }
}
BENCHMARK (MovementLongHaul)
  ->Unit (benchmark::kMillisecond)
  ->Args ({10})
  ->Args ({100})
  ->Args ({1'000});

} // anonymous namespace
} // namespace pxd
