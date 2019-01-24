#include "character.hpp"

#include "dbtest.hpp"
#include "schema.hpp"

#include <benchmark/benchmark.h>

#include <glog/logging.h>

#include <sstream>
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
      std::ostringstream name;
      name << "char " << i;

      const auto h = tbl.CreateNew ("domob", name.str (), Faction::RED);
      ids.push_back (h->GetId ());

      auto* wp = h->MutableProto ().mutable_movement ()->mutable_waypoints ();
      for (unsigned j = 0; j < numWP; ++j)
        wp->Add ()->set_x (j);
    }

  return ids;
}

/**
 * Benchmarks the lookup of characters from the database without any
 * modification.
 *
 * Arguments are:
 *  - Characters in the database
 *  - Characters to look up
 *  - Number of waypoints in the character proto (as a proxy for the data size
 *    of each character)
 */
void
CharacterLookup (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());

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
BENCHMARK (CharacterLookup)
  ->Unit (benchmark::kMicrosecond)
  ->Args ({10, 1, 0})
  ->Args ({10, 1, 10})
  ->Args ({10, 1, 100})
  ->Args ({10, 1, 1000})
  ->Args ({10, 10, 100})
  ->Args ({1000, 1, 100});

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
  SetupDatabaseSchema (db.GetHandle ());

  const unsigned n = state.range (0);
  const unsigned numWP = state.range (1);

  const auto charIds = InsertTestCharacters (db, n, numWP);
  CharacterTable tbl(db);

  int cnt = 0;
  for (auto _ : state)
    for (unsigned i = 0; i < n; ++i)
      {
        const auto h = tbl.GetById (charIds[i]);
        h->SetPartialStep (cnt++);
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
  SetupDatabaseSchema (db.GetHandle ());

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
        if (mv->steps_size () == 0)
          mv->mutable_steps ()->Add ();
        mv->mutable_steps (0)->set_x (cnt++);
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
