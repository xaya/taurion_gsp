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

#include "combat.hpp"

#include "database/character.hpp"
#include "database/damagelists.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
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
  CharacterTable tbl(db);

  for (unsigned i = 0; i < numIdle; ++i)
    {
      auto c = tbl.CreateNew ("domob", Faction::RED);
      c->MutableProto ().mutable_combat_data ();
      c.reset ();
    }

  for (unsigned i = 0; i < numTargets; ++i)
    {
      auto c = tbl.CreateNew ("domob", Faction::GREEN);
      const auto id = c->GetId ();
      auto& regen = c->MutableRegenData ();
      regen.set_shield_regeneration_mhp (1000);
      regen.mutable_max_hp ()->set_armour (targetHP);
      c->MutableHP ().set_armour (targetHP);
      c.reset ();

      c = tbl.CreateNew ("domob", Faction::RED);
      auto* cd = c->MutableProto ().mutable_combat_data ();
      for (unsigned j = 0; j < numAttacks; ++j)
        {
          auto* attack = cd->add_attacks ();
          attack->set_range (1);
          attack->set_min_damage (1);
          attack->set_max_damage (1);
        }
      auto* targetId = c->MutableProto ().mutable_target ();
      targetId->set_type (proto::TargetId::TYPE_CHARACTER);
      targetId->set_id (id);
      c.reset ();
    }
}

/**
 * Processes all "HP update" parts of the state update, as is done
 * also in the real state-update function.
 */
void
UpdateHP (Database& db, xaya::Random& rnd, const BaseMap& map)
{
  DamageLists dl(db, 0);

  const auto dead = DealCombatDamage (db, dl, rnd);
  ProcessKills (db, dl, dead, map);
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
  const BaseMap map;

  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());

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
      UpdateHP (db, rnd, map);
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
  const BaseMap map;

  TestDatabase db;
  SetupDatabaseSchema (db.GetHandle ());

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
      UpdateHP (db, rnd, map);
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
