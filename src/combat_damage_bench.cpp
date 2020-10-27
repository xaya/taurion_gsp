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

#include "combat.hpp"

#include "context.hpp"
#include "testutils.hpp"

#include "database/account.hpp"
#include "database/character.hpp"
#include "database/damagelists.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
#include "database/inventory.hpp"
#include "database/schema.hpp"
#include "hexagonal/coord.hpp"
#include "mapdata/basemap.hpp"
#include "proto/combat.pb.h"

#include <xayautil/hash.hpp>
#include <xayautil/random.hpp>

#include <benchmark/benchmark.h>

namespace pxd
{
namespace
{

/**
 * Creates test characters in the database.  We set up pairs of
 * characters where one attacks the other (with a preset target)
 * as well as characters that are "just there".
 */
void
InsertCharacters (Database& db, const unsigned numIdle,
                  const unsigned numTargets, const unsigned numAttacks,
                  const unsigned targetHP)
{
  AccountsTable acc(db);
  CharacterTable tbl(db);

  acc.CreateNew ("red")->SetFaction (Faction::RED);
  acc.CreateNew ("green")->SetFaction (Faction::GREEN);

  for (unsigned i = 0; i < numIdle; ++i)
    {
      auto c = tbl.CreateNew ("red", Faction::RED);
      c->MutableProto ().mutable_combat_data ();
      c.reset ();
    }

  for (unsigned i = 0; i < numTargets; ++i)
    {
      auto c = tbl.CreateNew ("green", Faction::GREEN);
      const auto id = c->GetId ();
      auto& regen = c->MutableRegenData ();
      regen.mutable_regeneration_mhp ()->set_shield (1'000);
      regen.mutable_max_hp ()->set_armour (targetHP);
      c->MutableHP ().set_armour (targetHP);
      c.reset ();

      c = tbl.CreateNew ("red", Faction::RED);
      auto* cd = c->MutableProto ().mutable_combat_data ();
      for (unsigned j = 0; j < numAttacks; ++j)
        {
          auto* attack = cd->add_attacks ();
          attack->set_range (1);
          attack->mutable_damage ()->set_min (1);
          attack->mutable_damage ()->set_max (1);
        }

      proto::TargetId t;
      t.set_type (proto::TargetId::TYPE_CHARACTER);
      t.set_id (id);
      c->SetTarget (t);
      c.reset ();
    }
}

/**
 * Processes all "HP update" parts of the state update, as is done
 * also in the real state-update function.
 */
void
UpdateHP (Database& db, xaya::Random& rnd, const Context& ctx)
{
  DamageLists dl(db, 0);
  GroundLootTable loot(db);

  const auto dead = DealCombatDamage (db, dl, rnd, ctx);
  ProcessKills (db, dl, loot, dead, rnd, ctx);
  RegenerateHP (db);
}

/**
 * Benchmarks dealing combat damage and regenerating, in the situation
 * that combat targets are not actually killed.
 *
 * The benchmark accepts the following arguments:
 *  - Number of attacking characters
 *  - Number of characters just being there
 *  - Number of attacks per character
 */
void
CombatHpUpdate (benchmark::State& state)
{
  ContextForTesting ctx;

  TestDatabase db;
  SetupDatabaseSchema (*db);

  xaya::SHA256 seed;
  seed << "random seed";
  xaya::Random rnd;
  rnd.Seed (seed.Finalise ());

  const unsigned numTargets = state.range (0);
  const unsigned numIdle = state.range (1);
  const unsigned numAttacks = state.range (2);

  InsertCharacters (db, numIdle, numTargets, numAttacks, 2 * numAttacks);

  for (auto _ : state)
    {
      TemporaryDatabaseChanges checkpoint(db, state);
      UpdateHP (db, rnd, ctx);
    }
}
BENCHMARK (CombatHpUpdate)
  ->Unit (benchmark::kMillisecond)
  ->Args ({100, 0, 1})
  ->Args ({100, 0, 10})
  ->Args ({10000, 0, 1})
  ->Args ({10000, 0, 10})
  ->Args ({100, 10000, 1})
  ->Args ({10000, 10000, 10});

/**
 * Benchmarks dealing combat damage in a situation where the target
 * gets killed.
 *
 * The benchmark accepts the following arguments:
 *  - Number of attacking characters
 *  - Number of characters just being there
 *  - Number of attacks per character
 */
void
CombatKills (benchmark::State& state)
{
  ContextForTesting ctx;

  TestDatabase db;
  SetupDatabaseSchema (*db);

  xaya::SHA256 seed;
  seed << "random seed";
  xaya::Random rnd;
  rnd.Seed (seed.Finalise ());

  const unsigned numTargets = state.range (0);
  const unsigned numIdle = state.range (1);
  const unsigned numAttacks = state.range (2);

  InsertCharacters (db, numIdle, numTargets, numAttacks, 1);

  for (auto _ : state)
    {
      TemporaryDatabaseChanges checkpoint(db, state);
      UpdateHP (db, rnd, ctx);
    }
}
BENCHMARK (CombatKills)
  ->Unit (benchmark::kMillisecond)
  ->Args ({100, 0, 1})
  ->Args ({100, 0, 10})
  ->Args ({10000, 0, 1})
  ->Args ({10000, 0, 10})
  ->Args ({100, 10000, 1})
  ->Args ({10000, 10000, 10});

} // anonymous namespace
} // namespace pxd
