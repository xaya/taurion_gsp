#include "movement.hpp"

#include "params.hpp"
#include "protoutils.hpp"

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
 * The speed of characters used in the benchmark.  The actual value does not
 * matter, since we match it to the edge weights such that the characters
 * move one tile per block.
 */
constexpr PathFinder::DistanceT SPEED = 1000;

PathFinder::DistanceT
EdgeWeights (const HexCoord& from, const HexCoord& to)
{
  return SPEED;
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

  const Params params(xaya::Chain::MAIN);

  const HexCoord::IntT numTiles = state.range (0);
  const unsigned numMoving = state.range (1);
  const unsigned numWP = state.range (2);

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
          h->SetPartialStep (0);
          h->SetPosition (HexCoord (0, 0));
          auto* mv = h->MutableProto ().mutable_movement ();
          mv->Clear ();
          auto* wp = mv->mutable_waypoints ();
          for (unsigned i = 0; i < numWP; ++i)
            *wp->Add () = CoordToProto (HexCoord (numTiles, 0));
        }
      state.ResumeTiming ();

      for (int i = 0; i < numTiles; ++i)
        ProcessAllMovement (db, params, &EdgeWeights);

      /* Make sure that we moved exactly the first part of the path.  This
         verifies that the benchmark is actually set up correctly, and ensures
         that we are not measuring something we don't want to measure.  */
      state.PauseTiming ();
      for (const auto id : charIds)
        {
          const auto h = tbl.GetById (id);
          CHECK (!h->GetProto ().has_movement ());
          CHECK_EQ (h->GetPosition (), HexCoord (numTiles, 0));
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

  const Params params(xaya::Chain::MAIN);

  const HexCoord::IntT wpDist = state.range (0);
  const HexCoord::IntT total = state.range (1);

  CharacterTable tbl(db);
  const auto id = CreateCharacter (tbl)->GetId ();

  for (auto _ : state)
    {
      state.PauseTiming ();
      {
        const auto h = tbl.GetById (id);
        h->SetPartialStep (0);
        h->SetPosition (HexCoord (0, 0));
        auto* mv = h->MutableProto ().mutable_movement ();
        mv->Clear ();
        auto* wp = mv->mutable_waypoints ();

        HexCoord::IntT lastX = 0;
        while (lastX != total)
          {
            lastX += wpDist;
            if (lastX > total)
              lastX = total;
            *wp->Add () = CoordToProto (HexCoord (lastX, 0));
          }
        LOG (INFO)
            << "Using " << wp->size () << " waypoints to move " << total
            << " tiles with spacing of " << wpDist;
      }
      state.ResumeTiming ();

      do
        {
          ProcessAllMovement (db, params, &EdgeWeights);
        }
      while (tbl.GetById (id)->GetProto ().has_movement ());

      state.PauseTiming ();
      CHECK_EQ (tbl.GetById (id)->GetPosition (), HexCoord (total, 0));
      state.ResumeTiming ();
    }
}
BENCHMARK (MovementLongHaul)
  ->Unit (benchmark::kMillisecond)
  ->Args ({10, 100})
  ->Args ({10, 1000})
  ->Args ({10, 10000})
  ->Args ({100, 100})
  ->Args ({100, 1000})
  ->Args ({100, 10000});

} // anonymous namespace
} // namespace pxd
