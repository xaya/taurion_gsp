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
constexpr PathFinder::DistanceT SPEED = 1000;

/**
 * Initialises the test account in the database.
 */
void
InitialiseAccount (Database& db)
{
  AccountsTable tbl(db);
  tbl.CreateNew ("domob", Faction::RED);
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
 * Benchmarks movement along one segment (path finding and then stepping).
 * One iteration of the benchmark corresponds to moving for N blocks,
 * where N is the distance used between waypoints.
 *
 * The benchmark accepts the following arguments:
 *  - The distance to use between waypoints
 *  - The number of characters to move around
 *  - The number of waypoints to set for each character (this only affects
 *    the size of the protocol buffer data)
 */
void
MovementOneSegment (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());

  ContextForTesting ctx;

  const HexCoord::IntT numTiles = state.range (0);
  const unsigned numMoving = state.range (1);
  const unsigned numWP = state.range (2);

  InitialiseAccount (db);

  CharacterTable tbl(db);
  std::vector<Database::IdT> charIds;
  for (unsigned i = 0; i < numMoving; ++i)
    charIds.push_back (CreateCharacter (tbl)->GetId ());

  for (auto _ : state)
    {
      state.PauseTiming ();
      for (const auto id : charIds)
        {
          const auto h = tbl.GetById (id);
          StopCharacter (*h);
          h->SetPosition (HexCoord (0, id));
          auto* mv = h->MutableProto ().mutable_movement ();
          auto* wp = mv->mutable_waypoints ();
          for (unsigned i = 0; i < numWP; ++i)
            *wp->Add () = CoordToProto (HexCoord (numTiles, id));
        }
      DynObstacles dyn(db);
      state.ResumeTiming ();

      for (int i = 0; i < numTiles; ++i)
        ProcessAllMovement (db, dyn, ctx);

      /* Make sure that we moved exactly the first part of the path.  This
         verifies that the benchmark is actually set up correctly, and ensures
         that we are not measuring something we don't want to measure.  */
      state.PauseTiming ();
      for (const auto id : charIds)
        {
          const auto h = tbl.GetById (id);
          CHECK (!h->GetProto ().has_movement ());
          CHECK_EQ (h->GetPosition (), HexCoord (numTiles, id));
        }
      state.ResumeTiming ();
    }
}
BENCHMARK (MovementOneSegment)
  ->Unit (benchmark::kMillisecond)
  ->Args ({10, 1, 1})
  ->Args ({10, 10, 1})
  ->Args ({10, 1, 100})
  ->Args ({100, 1, 1})
  ->Args ({100, 10, 1})
  ->Args ({100, 1, 100});

/**
 * Benchmarks moving a single character a long way, including over multiple
 * way segments.  This gives us an idea of how costly long-haul travel is
 * in general.
 *
 * Arguments are:
 *  - Distance between waypoints
 *  - Total distance travelled
 */
void
MovementLongHaul (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());

  ContextForTesting ctx;

  const HexCoord::IntT wpDist = state.range (0);
  const HexCoord::IntT total = state.range (1);

  InitialiseAccount (db);

  CharacterTable tbl(db);
  const auto id = CreateCharacter (tbl)->GetId ();

  /* We start travelling from a non-zero origin.  The coordinate is chosen
     such that movement from it in positive x direction is free for a long
     enough path.  */
  const HexCoord origin(1000, -2636);

  for (auto _ : state)
    {
      state.PauseTiming ();
      {
        const auto h = tbl.GetById (id);
        StopCharacter (*h);
        h->SetPosition (origin);
        auto* mv = h->MutableProto ().mutable_movement ();
        auto* wp = mv->mutable_waypoints ();

        HexCoord::IntT lastX = 0;
        while (lastX != total)
          {
            lastX += wpDist;
            if (lastX > total)
              lastX = total;

            HexCoord nextWp(lastX, 0);
            nextWp += origin;
            *wp->Add () = CoordToProto (nextWp);
          }
        LOG (INFO)
            << "Using " << wp->size () << " waypoints to move " << total
            << " tiles with spacing of " << wpDist;
      }
      DynObstacles dyn(db);
      state.ResumeTiming ();

      do
        {
          ProcessAllMovement (db, dyn, ctx);
        }
      while (tbl.GetById (id)->GetProto ().has_movement ());

      state.PauseTiming ();
      HexCoord expected(total, 0);
      expected += origin;
      CHECK_EQ (tbl.GetById (id)->GetPosition (), expected);
      state.ResumeTiming ();
    }
}
BENCHMARK (MovementLongHaul)
  ->Unit (benchmark::kMillisecond)
  ->Args ({10, 100})
  ->Args ({10, 1000})
  ->Args ({100, 100})
  ->Args ({100, 1000});

} // anonymous namespace
} // namespace pxd
