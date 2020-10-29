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

#include "character.hpp"

#include "dbtest.hpp"
#include "schema.hpp"

#include <benchmark/benchmark.h>

#include <glog/logging.h>

#include <vector>

namespace pxd
{
namespace
{

/**
 * Adds n test characters with the numWP waypoints each into the database.
 * Returns a vector of their IDs.
 */
std::vector<Database::IdT>
InsertTestCharacters (Database& db, const unsigned n, const unsigned numWP)
{
  CharacterTable tbl(db);

  std::vector<Database::IdT> ids;
  for (unsigned i = 0; i < n; ++i)
    {
      const auto h = tbl.CreateNew ("domob", Faction::RED);
      ids.push_back (h->GetId ());

      auto* wp = h->MutableProto ().mutable_movement ()->mutable_waypoints ();
      for (unsigned j = 0; j < numWP; ++j)
        wp->Add ()->set_x (j);
    }

  return ids;
}

/**
 * Benchmarks the lookup of characters from the database without any
 * modification nor an access to a proto field.
 *
 * Arguments are:
 *  - Characters in the database
 *  - Characters to look up
 *  - Number of waypoints in the character proto (as a proxy for the data size
 *    of each character)
 */
void
CharacterLookupSimple (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (*db);

  const unsigned numInDb = state.range (0);
  const unsigned numLookedUp = state.range (1);
  const unsigned numWP = state.range (2);
  CHECK_LE (numLookedUp, numInDb);

  const auto charIds = InsertTestCharacters (db, numInDb, numWP);
  CharacterTable tbl(db);

  for (auto _ : state)
    for (unsigned i = 0; i < numLookedUp; ++i)
      {
        const auto h = tbl.GetById (charIds[i]);
        CHECK_EQ (h->GetId (), charIds[i]);
      }
}
BENCHMARK (CharacterLookupSimple)
  ->Unit (benchmark::kMicrosecond)
  ->Args ({10, 1, 0})
  ->Args ({10, 1, 100})
  ->Args ({10, 1, 1000})
  ->Args ({10, 10, 100})
  ->Args ({1000, 1, 100});

/**
 * Benchmarks the lookup of characters from the database without any
 * modification but with access to the main proto field.
 *
 * Arguments are:
 *  - Characters to look up
 *  - Number of waypoints in the character proto (as a proxy for the data size
 *    of each character)
 */
void
CharacterLookupProto (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (*db);

  const unsigned numChar = state.range (0);
  const unsigned numWP = state.range (1);

  const auto charIds = InsertTestCharacters (db, numChar, numWP);
  CharacterTable tbl(db);

  for (auto _ : state)
    for (const auto id : charIds)
      {
        const auto h = tbl.GetById (id);
        CHECK_EQ (h->GetId (), id);
        CHECK (h->GetProto ().has_movement ());
      }
}
BENCHMARK (CharacterLookupProto)
  ->Unit (benchmark::kMicrosecond)
  ->Args ({1, 0})
  ->Args ({1, 10})
  ->Args ({1, 100})
  ->Args ({1, 1000})
  ->Args ({10, 100});

/**
 * Benchmarks the lookup of characters from the database while looping
 * through a single result set.
 *
 * Arguments are:
 *  - Characters to look up
 *  - Number of waypoints in each character proto
 */
void CharacterQuery (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (*db);

  const unsigned numChar = state.range (0);
  const unsigned numWP = state.range (1);

  InsertTestCharacters (db, numChar, numWP);
  CharacterTable tbl(db);

  for (auto _ : state)
    {
      auto res = tbl.QueryAll ();
      while (res.Step ())
        tbl.GetFromResult (res);
    }
}
BENCHMARK (CharacterQuery)
  ->Unit (benchmark::kMillisecond)
  ->Args ({100, 0})
  ->Args ({100, 10})
  ->Args ({100, 100})
  ->Args ({100, 1000})
  ->Args ({1000, 10})
  ->Args ({10000, 10})
  ->Args ({100000, 10});

/**
 * Benchmarks updates to characters that do not touch the proto data
 * (just the database fields themselves).
 *
 * Arguments are:
 *  - Characters to update
 *  - Number of waypoints for each character
 */
void
CharacterFieldsUpdate (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (*db);

  const unsigned n = state.range (0);
  const unsigned numWP = state.range (1);

  const auto charIds = InsertTestCharacters (db, n, numWP);
  CharacterTable tbl(db);

  int cnt = 0;
  for (auto _ : state)
    for (unsigned i = 0; i < n; ++i)
      {
        const auto h = tbl.GetById (charIds[i]);
        h->MutableVolatileMv ().set_partial_step (cnt++);
        h->MutableHP ().set_armour (42);
      }
}
BENCHMARK (CharacterFieldsUpdate)
  ->Unit (benchmark::kMicrosecond)
  ->Args ({1, 0})
  ->Args ({1, 10})
  ->Args ({1, 100})
  ->Args ({1, 1000})
  ->Args ({10, 100});

/**
 * Benchmarks updates to characters that modify the proto data and thus
 * require a full update.
 *
 * Arguments are:
 *  - Characters to update
 *  - Number of waypoints for each character
 */
void
CharacterProtoUpdate (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (*db);

  const unsigned n = state.range (0);
  const unsigned numWP = state.range (1);

  const auto charIds = InsertTestCharacters (db, n, numWP);
  CharacterTable tbl(db);

  int cnt = 0;
  for (auto _ : state)
    for (unsigned i = 0; i < n; ++i)
      {
        const auto h = tbl.GetById (charIds[i]);
        auto* mv = h->MutableProto ().mutable_movement ();
        if (mv->waypoints_size () == 0)
          mv->mutable_waypoints ()->Add ();
        mv->mutable_waypoints (0)->set_x (cnt++);
      }
}
BENCHMARK (CharacterProtoUpdate)
  ->Unit (benchmark::kMicrosecond)
  ->Args ({1, 0})
  ->Args ({1, 10})
  ->Args ({1, 100})
  ->Args ({1, 1000})
  ->Args ({10, 100});

} // anonymous namespace
} // namespace pxd
