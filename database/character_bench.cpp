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

/**
 * Benchmarks the decrement busy function.  The time reported is the time
 * it takes to perform 1,000 decrement operations for the full set of
 * specified characters.
 *
 * Arguments are:
 *  - Number of characters that are busy (and thus updated)
 *  - Number of characters with busy=0 (ignored)
 */
void
DecrementBusy (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());
  CharacterTable tbl(db);

  const unsigned numBusy = state.range (0);
  const unsigned numNonBusy = state.range (1);

  std::vector<Database::IdT> busyIds;
  busyIds.reserve (numBusy);
  for (unsigned i = 0; i < numBusy; ++i)
    busyIds.push_back (tbl.CreateNew ("domob", Faction::RED)->GetId ());
  for (unsigned i = 0; i < numNonBusy; ++i)
    tbl.CreateNew ("domob", Faction::GREEN);

  for (auto _ : state)
    {
      /* Reset the busy state here to make sure that it will never run down
         to zero (independently of how many iterations the benchmark runs).  */
      state.PauseTiming ();
      for (const auto id : busyIds)
        {
          auto c = tbl.GetById (id);
          c->SetBusy (2000 + id);
          c->MutableProto ().mutable_prospection ();
        }
      state.ResumeTiming ();

      for (unsigned i = 0; i < 1000; ++i)
        tbl.DecrementBusy ();
    }
}
BENCHMARK (DecrementBusy)
  ->Unit (benchmark::kMillisecond)
  ->Args ({1, 0})
  ->Args ({100, 0})
  ->Args ({1, 100000})
  ->Args ({100, 100000});

} // anonymous namespace
} // namespace pxd
