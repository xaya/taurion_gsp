#include "target.hpp"

#include "character.hpp"
#include "dbtest.hpp"
#include "schema.hpp"

#include "hexagonal/coord.hpp"

#include <benchmark/benchmark.h>

#include <glog/logging.h>

#include <sstream>

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
      std::ostringstream name;
      name << "char " << i << " @ " << pos;

      auto h = tbl.CreateNew ("domob", name.str (), f);
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
 */
void
TargetFinding (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());

  const HexCoord::IntT range = state.range (0);
  const unsigned inRange = state.range (1);
  const unsigned sameX = state.range (2);
  const unsigned sameY = state.range (3);

  InsertTestCharacters (db, inRange, HexCoord (0, 0), Faction::GREEN);
  InsertTestCharacters (db, sameX, HexCoord (0, 2 * range), Faction::GREEN);
  InsertTestCharacters (db, sameY, HexCoord (2 * range, 0), Faction::GREEN);

  TargetFinder finder(db);
  for (auto _ : state)
    {
      unsigned cnt = 0;
      TargetFinder::ProcessingFcn cb = [&cnt] (const HexCoord& c,
                                               const proto::TargetId& t)
        {
          ++cnt;
        };

      finder.ProcessL1Targets (HexCoord (0, 0), range, Faction::RED, cb);

      CHECK_EQ (cnt, inRange);
    }
}
BENCHMARK (TargetFinding)
  ->Unit (benchmark::kMicrosecond)
  ->Args ({10, 1, 0, 0})
  ->Args ({10, 100, 0, 0})
  ->Args ({10, 10000, 0, 0})
  ->Args ({100, 1, 0, 0})
  ->Args ({100, 100, 0, 0})
  ->Args ({100, 10000, 0, 0})
  ->Args ({10, 100, 10000, 0})
  ->Args ({10, 100, 0, 10000});

} // anonymous namespace
} // namespace pxd
