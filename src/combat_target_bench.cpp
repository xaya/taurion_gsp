#include "combat.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
#include "database/schema.hpp"
#include "hexagonal/coord.hpp"

#include <xayagame/hash.hpp>
#include <xayagame/random.hpp>

#include <benchmark/benchmark.h>

#include <sstream>

namespace pxd
{
namespace
{

/**
 * Creates test characters in the database.  We create "stacks" of k
 * characters each in a (rows x cols) grid.  The individual stacks are 100
 * tiles apart from each other.
 */
void
InsertCharacters (Database& db, const Faction f,
                  const unsigned k,
                  const unsigned rows, const unsigned cols)
{
  constexpr HexCoord::IntT spacing = 100;

  CharacterTable tbl(db);

  static unsigned cnt = 0;
  for (unsigned r = 0; r < rows; ++r)
    for (unsigned c = 0; c < cols; ++c)
      {
        const HexCoord pos(c * spacing, r * spacing);
        for (unsigned i = 0; i < k; ++i)
          {
            std::ostringstream name;
            name << "test character " << ++cnt;

            auto c = tbl.CreateNew ("domob", name.str (), f);
            c->SetPosition (pos);
            auto& pb = c->MutableProto ();
            auto* attack = pb.mutable_combat_data ()->add_attacks ();
            attack->set_range (10);
            c.reset ();
          }
      }
}

/**
 * Benchmarks combat target selection for the situation where we have
 * many characters but they are not enemies to each other.
 *
 * The benchmark accepts the following arguments:
 *  - Number of characters on each stack
 *  - Number of "rows" for stacks
 *  - Number of "columns" for stacks
 */
void
TargetSelectionFriendly (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());

  xaya::SHA256 seed;
  seed << "random seed";
  xaya::Random rnd;
  rnd.Seed (seed.Finalise ());

  const unsigned perStack = state.range (0);
  const unsigned rows = state.range (1);
  const unsigned cols = state.range (2);

  const unsigned numStacks = rows * cols;
  LOG (INFO)
      << "Benchmarking " << numStacks << " stacks with " << perStack
      << " characters each";
  LOG (INFO) << "Total characters: " << (numStacks * perStack);

  InsertCharacters (db, Faction::RED, perStack, rows, cols);

  for (auto _ : state)
    FindCombatTargets (db, rnd);
}
BENCHMARK (TargetSelectionFriendly)
  ->Unit (benchmark::kMillisecond)
  ->Args ({10, 1, 1})
  ->Args ({100, 1, 1})
  ->Args ({1000, 1, 1})
  ->Args ({100, 10, 1})
  ->Args ({100, 1, 10})
  ->Args ({10, 10, 100})
  ->Args ({10, 100, 10});

/**
 * Benchmarks combat target selection for the situation where we have
 * many characters of mixed factions (i.e. they really target each other).
 * Each stack of N characters will contain N/2 of one of two factions.
 *
 * The benchmark accepts the following arguments:
 *  - Number of characters on each stack
 *  - Number of "rows" for stacks
 *  - Number of "columns" for stacks
 */
void
TargetSelectionEnemies (benchmark::State& state)
{
  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());

  xaya::SHA256 seed;
  seed << "random seed";
  xaya::Random rnd;
  rnd.Seed (seed.Finalise ());

  const unsigned perStack = state.range (0);
  const unsigned rows = state.range (1);
  const unsigned cols = state.range (2);

  const unsigned numStacks = rows * cols;
  LOG (INFO)
      << "Benchmarking " << numStacks << " stacks with " << perStack
      << " characters each";
  LOG (INFO) << "Total characters: " << (numStacks * perStack);

  CHECK_EQ (perStack % 2, 0);
  InsertCharacters (db, Faction::RED, perStack / 2, rows, cols);
  InsertCharacters (db, Faction::GREEN, perStack / 2, rows, cols);

  for (auto _ : state)
    FindCombatTargets (db, rnd);
}
BENCHMARK (TargetSelectionEnemies)
  ->Unit (benchmark::kMillisecond)
  ->Args ({10, 1, 1})
  ->Args ({100, 1, 1})
  ->Args ({1000, 1, 1})
  ->Args ({100, 10, 1})
  ->Args ({100, 1, 10})
  ->Args ({10, 10, 100})
  ->Args ({10, 100, 10});

} // anonymous namespace
} // namespace pxd
