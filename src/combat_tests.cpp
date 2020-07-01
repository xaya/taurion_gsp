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

#include "testutils.hpp"

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/damagelists.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"
#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <xayautil/hash.hpp>
#include <xayautil/random.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <glog/logging.h>

#include <map>
#include <vector>

namespace pxd
{
namespace
{

using testing::ElementsAre;
using testing::ElementsAreArray;

/** A coordinate that is a safe zone.  */
const HexCoord SAFE(2'042, 10);
/** A coordinate that is not safe (but next to the safe one).  */
const HexCoord NOT_SAFE(2'042, 11);
/** A coordinate that is not safe and a bit further away.  */
const HexCoord NOT_SAFE_FURTHER(2'042, 15);

/* ************************************************************************** */

class CombatTests : public DBTestWithSchema
{

protected:

  ContextForTesting ctx;

  BuildingsTable buildings;
  BuildingInventoriesTable inventories;

  CharacterTable characters;
  DamageLists dl;
  TestRandom rnd;

  CombatTests ()
    : buildings(db), inventories(db), characters(db), dl(db, 0)
  {
    /* Ensure our hardcoded test data for safe zones is correct.  */
    CHECK (ctx.Map ().SafeZones ().IsNoCombat (SAFE));
    CHECK (!ctx.Map ().SafeZones ().IsNoCombat (NOT_SAFE));
    CHECK (!ctx.Map ().SafeZones ().IsNoCombat (NOT_SAFE_FURTHER));
    CHECK_EQ (HexCoord::DistanceL1 (SAFE, NOT_SAFE), 1);
    CHECK_GT (HexCoord::DistanceL1 (NOT_SAFE, NOT_SAFE_FURTHER), 1);
  }

  /**
   * Adds an attack without any more other stats to the combat entity and
   * returns a reference to it for further customisation.
   */
  template <typename T>
    static proto::Attack&
    AddAttack (T& h)
  {
    return *h.MutableProto ().mutable_combat_data ()->add_attacks ();
  }

  /**
   * Initialises the combat data proto so that it is "valid" but has
   * no attacks.
   */
  static void
  NoAttacks (Character& c)
  {
    c.MutableProto ().mutable_combat_data ();
  }

  /**
   * Sets HP and max HP of a character.
   */
  static void
  SetHp (Character& c, const unsigned shield, const unsigned armour,
         const unsigned maxShield, const unsigned maxArmour)
  {
    c.MutableHP ().set_shield (shield);
    c.MutableHP ().set_armour (armour);
    c.MutableRegenData ().mutable_max_hp ()->set_shield (maxShield);
    c.MutableRegenData ().mutable_max_hp ()->set_armour (maxArmour);
  }

  /**
   * Adds a low-HP boost for the given character.  It will apply to range
   * and damage with the same boost for simplicity.
   */
  static void
  AddLowHpBoost (Character& c, const unsigned maxHpPercent,
                 const unsigned boostPercent)
  {
    proto::LowHpBoost boost;
    boost.set_max_hp_percent (maxHpPercent);
    boost.mutable_range ()->set_percent (boostPercent);
    boost.mutable_damage ()->set_percent (boostPercent);

    *c.MutableProto ().mutable_combat_data ()->add_low_hp_boosts () = boost;
  }

  /**
   * Adds a self-destruct ability for the given character.
   */
  static void
  AddSelfDestruct (Character& c, const unsigned area, const unsigned dmg)
  {
    proto::SelfDestruct sd;
    sd.set_area (area);
    sd.mutable_damage ()->set_min (dmg);
    sd.mutable_damage ()->set_max (dmg);

    *c.MutableProto ().mutable_combat_data ()->add_self_destructs () = sd;
  }

};

/* ************************************************************************** */

using TargetSelectionTests = CombatTests;

TEST_F (TargetSelectionTests, NoTargets)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (-10, 0));
  proto::TargetId t;
  t.set_id (42);
  c->SetTarget (t);
  AddAttack (*c).set_range (10);
  c.reset ();

  c = characters.CreateNew ("domob",Faction::RED);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (-10, 1));
  c->SetTarget (t);
  AddAttack (*c).set_range (10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id3 = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  c->SetTarget (t);
  AddAttack (*c).set_range (10);
  c.reset ();

  FindCombatTargets (db, rnd, ctx);

  EXPECT_FALSE (characters.GetById (id1)->HasTarget ());
  EXPECT_FALSE (characters.GetById (id2)->HasTarget ());
  EXPECT_FALSE (characters.GetById (id3)->HasTarget ());
}

TEST_F (TargetSelectionTests, ClosestTarget)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttack (*c).set_range (10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  c->SetPosition (HexCoord (2, 2));
  AddAttack (*c).set_range (10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (1, 1));
  AddAttack (*c).set_range (10);
  c.reset ();

  /* Since target selection is randomised, run this multiple times to ensure
     that we always pick the same target (single closest one).  */
  for (unsigned i = 0; i < 100; ++i)
    {
      FindCombatTargets (db, rnd, ctx);

      c = characters.GetById (idFighter);
      const auto& t = c->GetTarget ();
      EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
      EXPECT_EQ (t.id (), idTarget);
    }
}

TEST_F (TargetSelectionTests, ZeroRange)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttack (*c).set_range (0);
  c.reset ();

  c = characters.CreateNew ("andy", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (1, 0));
  NoAttacks (*c);
  c.reset ();

  FindCombatTargets (db, rnd, ctx);

  EXPECT_FALSE (characters.GetById (idFighter)->HasTarget ());
  characters.GetById (idTarget)->SetPosition (HexCoord (0, 0));

  FindCombatTargets (db, rnd, ctx);

  c = characters.GetById (idFighter);
  ASSERT_TRUE (c->HasTarget ());
  const auto& t = c->GetTarget ();
  EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (t.id (), idTarget);
}

TEST_F (TargetSelectionTests, WithBuildings)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idChar = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttack (*c).set_range (10);
  c.reset ();

  auto b = buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  b->SetCentre (HexCoord (0, 0));
  b.reset ();

  b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  b->SetCentre (HexCoord (0, -1));
  b.reset ();

  b = buildings.CreateNew ("checkmark", "domob", Faction::GREEN);
  const auto idBuilding = b->GetId ();
  b->SetCentre (HexCoord (0, 2));
  AddAttack (*b).set_range (10);
  b.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  c->SetPosition (HexCoord (0, 3));
  c.reset ();

  FindCombatTargets (db, rnd, ctx);

  c = characters.GetById (idChar);
  const auto* t = &c->GetTarget ();
  EXPECT_EQ (t->type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (t->id (), idBuilding);

  b = buildings.GetById (idBuilding);
  t = &b->GetTarget ();
  EXPECT_EQ (t->type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (t->id (), idChar);
}

TEST_F (TargetSelectionTests, InsideBuildings)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  proto::TargetId t;
  t.set_id (42);
  c->SetTarget (t);
  AddAttack (*c).set_range (10);
  c.reset ();

  c = characters.CreateNew ("domob",Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetBuildingId (100);
  /* This character will not be processed for target finding, so an existing
     target will not actually be cleared.  (But a new one should also not be
     added to it.)  We clear the target when the character enters a
     building.  */
  AddAttack (*c).set_range (10);
  c.reset ();

  FindCombatTargets (db, rnd, ctx);

  EXPECT_FALSE (characters.GetById (id1)->HasTarget ());
  EXPECT_FALSE (characters.GetById (id2)->HasTarget ());
}

TEST_F (TargetSelectionTests, SafeZone)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idSafe = c->GetId ();
  c->SetPosition (SAFE);
  proto::TargetId t;
  t.set_id (42);
  c->SetTarget (t);
  AddAttack (*c).set_range (10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idAttacker = c->GetId ();
  c->SetPosition (NOT_SAFE);
  AddAttack (*c).set_range (10);
  c.reset ();

  /* This one is not in a safe zone and thus a valid target for idAttacker.
     It is further away than the one in the safe zone, so that it would
     normally not be selected (if the safe zone weren't there).  */
  c = characters.CreateNew ("domob", Faction::RED);
  const auto idTarget = c->GetId ();
  c->SetPosition (NOT_SAFE_FURTHER);
  NoAttacks (*c);
  c.reset ();

  FindCombatTargets (db, rnd, ctx);

  EXPECT_FALSE (characters.GetById (idSafe)->HasTarget ());
  c = characters.GetById (idAttacker);
  ASSERT_TRUE (c->HasTarget ());
  EXPECT_EQ (c->GetTarget ().id (), idTarget);
}

TEST_F (TargetSelectionTests, MultipleAttacks)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttack (*c).set_range (1);
  AddAttack (*c).set_range (10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (7, 0));
  NoAttacks (*c);
  c.reset ();

  FindCombatTargets (db, rnd, ctx);

  c = characters.GetById (id1);
  const auto& t = c->GetTarget ();
  EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (t.id (), id2);

  EXPECT_FALSE (characters.GetById (id2)->HasTarget ());
}

TEST_F (TargetSelectionTests, OnlyAreaAttacks)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttack (*c).set_area (7);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (7, 0));
  AddAttack (*c).set_area (6);
  c.reset ();

  FindCombatTargets (db, rnd, ctx);

  c = characters.GetById (id1);
  const auto& t = c->GetTarget ();
  EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (t.id (), id2);

  EXPECT_FALSE (characters.GetById (id2)->HasTarget ());
}

TEST_F (TargetSelectionTests, Randomisation)
{
  constexpr unsigned nTargets = 5;
  constexpr unsigned rolls = 1000;
  constexpr unsigned threshold = rolls / nTargets * 80 / 100;

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttack (*c).set_range (10);
  c.reset ();

  std::map<Database::IdT, unsigned> targetMap;
  for (unsigned i = 0; i < nTargets; ++i)
    {
      c = characters.CreateNew ("domob", Faction::GREEN);
      targetMap.emplace (c->GetId (), i);
      c->SetPosition (HexCoord (1, 1));
      NoAttacks (*c);
      c.reset ();
    }
  ASSERT_EQ (targetMap.size (), nTargets);

  std::vector<unsigned> cnt(nTargets);
  for (unsigned i = 0; i < rolls; ++i)
    {
      FindCombatTargets (db, rnd, ctx);

      c = characters.GetById (idFighter);
      const auto& t = c->GetTarget ();
      EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);

      const auto mit = targetMap.find (t.id ());
      ASSERT_NE (mit, targetMap.end ());
      ++cnt[mit->second];
    }

  for (unsigned i = 0; i < nTargets; ++i)
    {
      LOG (INFO) << "Target " << i << " was selected " << cnt[i] << " times";
      EXPECT_GE (cnt[i], threshold);
    }
}

TEST_F (TargetSelectionTests, LowHpBoost)
{
  auto c = characters.CreateNew ("boosted", Faction::RED);
  const auto idBoosted = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  SetHp (*c, 0, 100, 0, 1'000);
  AddAttack (*c).set_range (10);
  AddLowHpBoost (*c, 10, 10);
  c.reset ();

  c = characters.CreateNew ("boosted area", Faction::RED);
  const auto idArea = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  SetHp (*c, 0, 100, 0, 1'000);
  AddAttack (*c).set_area (10);
  AddLowHpBoost (*c, 10, 10);
  c.reset ();

  c = characters.CreateNew ("normal", Faction::GREEN);
  const auto idNormal = c->GetId ();
  c->SetPosition (HexCoord (11, 0));
  SetHp (*c, 0, 101, 0, 1'000);
  AddAttack (*c).set_range (10);
  AddLowHpBoost (*c, 10, 10);
  c.reset ();

  FindCombatTargets (db, rnd, ctx);
  EXPECT_FALSE (characters.GetById (idNormal)->HasTarget ());
  EXPECT_EQ (characters.GetById (idBoosted)->GetTarget ().id (), idNormal);
  EXPECT_EQ (characters.GetById (idArea)->GetTarget ().id (), idNormal);
}

/* ************************************************************************** */

class DealDamageTests : public CombatTests
{

protected:

  /**
   * Adds an attack with the given range and damage.  Returns a reference
   * to the added Attack proto so it can be further tweaked.
   */
  static proto::Attack&
  AddAttack (Character& c, const HexCoord::IntT range,
             const unsigned minDmg, const unsigned maxDmg)
  {
    auto& attack = CombatTests::AddAttack (c);
    attack.set_range (range);
    attack.mutable_damage ()->set_min (minDmg);
    attack.mutable_damage ()->set_max (maxDmg);
    return attack;
  }

  /**
   * Adds an area attack with the given range and damage.  Returns a reference
   * to the added Attack proto so it can be further tweaked.
   */
  static proto::Attack&
  AddAreaAttack (Character& c, const HexCoord::IntT range,
                 const unsigned minDmg, const unsigned maxDmg)
  {
    auto& attack = CombatTests::AddAttack (c);
    attack.set_area (range);
    attack.mutable_damage ()->set_min (minDmg);
    attack.mutable_damage ()->set_max (maxDmg);
    return attack;
  }

  /**
   * Finds combat targets and deals damage.
   */
  std::set<TargetKey>
  FindTargetsAndDamage ()
  {
    FindCombatTargets (db, rnd, ctx);
    return DealCombatDamage (db, dl, rnd, ctx);
  }

};

TEST_F (DealDamageTests, NoAttacks)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  NoAttacks (*c);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  NoAttacks (*c);
  SetHp (*c, 0, 10, 0, 10);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 10);
}

TEST_F (DealDamageTests, OnlyAttacksInRange)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (*c, 1, 1, 1);
  AddAttack (*c, 2, 1, 1);
  AddAttack (*c, 3, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (2, 0));
  NoAttacks (*c);
  SetHp (*c, 0, 10, 0, 10);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 8);
}

TEST_F (DealDamageTests, AreaAttacks)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  AddAreaAttack (*c, 10, 1, 2);
  c.reset ();

  std::vector<Database::IdT> idTargets;
  for (unsigned i = 0; i < 10; ++i)
    {
      c = characters.CreateNew ("green", Faction::GREEN);
      idTargets.push_back (c->GetId ());
      NoAttacks (*c);
      SetHp (*c, 0, 1'000, 0, 1'000);
      c.reset ();
    }

  /* The single attack should do randomised but per-turn consistent damage
     to all of the targets.  */

  unsigned cnt[] = {0, 0, 0};
  constexpr unsigned trials = 100;
  for (unsigned i = 0; i < trials; ++i)
    {
      const auto oldHP = characters.GetById (idTargets[0])->GetHP ().armour ();
      FindTargetsAndDamage ();

      const auto newHP = characters.GetById (idTargets[0])->GetHP ().armour ();
      for (const auto id : idTargets)
        ASSERT_EQ (characters.GetById (id)->GetHP ().armour (), newHP);

      ++cnt[oldHP - newHP];
    }

  EXPECT_EQ (cnt[1] + cnt[2], trials);
  EXPECT_GT (cnt[1], 0);
  EXPECT_GT (cnt[2], 0);
}

TEST_F (DealDamageTests, AreaAroundTarget)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  AddAreaAttack (*c, 5, 1, 1).set_range (10);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idArea = c->GetId ();
  c->SetPosition (HexCoord (10, 5));
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  FindTargetsAndDamage ();

  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 99);
  EXPECT_EQ (characters.GetById (idArea)->GetHP ().armour (), 99);
}

TEST_F (DealDamageTests, AreaTargetTooFar)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  /* We have one normal attack and a long range, which targets a character
     and hits it.  But the area attacks have shorter range, so they won't
     damage the target further.  */
  AddAttack (*c, 10, 1, 1);
  AddAreaAttack (*c, 5, 10, 10);
  AddAreaAttack (*c, 5, 10, 10).set_range (5);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 99);
}

TEST_F (DealDamageTests, MixedAttacks)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  AddAreaAttack (*c, 5, 1, 1);
  AddAttack (*c, 10, 1, 1);
  AddAreaAttack (*c, 10, 1, 1);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTargetNear = c->GetId ();
  c->SetPosition (HexCoord (5, 0));
  NoAttacks (*c);
  SetHp (*c, 0, 10, 0, 10);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTargetFar = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  NoAttacks (*c);
  SetHp (*c, 0, 10, 0, 10);
  c.reset ();

  /* Near and far take respective damage from the area attacks (near two
     and far one point), and one of them (randomly) takes damage also from
     the non-area attack.  */
  FindTargetsAndDamage ();
  const auto hpNear = characters.GetById (idTargetNear)->GetHP ().armour ();
  const auto hpFar = characters.GetById (idTargetFar)->GetHP ().armour ();
  EXPECT_EQ (hpNear + hpFar, 2 * 10 - 2 - 1 - 1);
  EXPECT_GE (hpNear, 7);
  EXPECT_LE (hpNear, 8);
  EXPECT_GE (hpFar, 8);
  EXPECT_LE (hpFar, 9);
}

TEST_F (DealDamageTests, SafeZone)
{
  /* One attacker has both area and normal attacks and also a slowing effect.
     It is not in the safe zone.  A potential target is in the safe zone and
     one outside.  The outside target should be hit by all attacks, and the
     safe-zone character by none.  */

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idSafe = c->GetId ();
  c->SetPosition (SAFE);
  NoAttacks (*c);
  SetHp (*c, 0, 10, 0, 10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  c->SetPosition (NOT_SAFE);
  AddAttack (*c, 10, 1, 1);
  AddAreaAttack (*c, 10, 1, 1);
  auto& attack = CombatTests::AddAttack (*c);
  attack.set_area (10);
  attack.mutable_effects ()->mutable_speed ()->set_percent (-50);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  const auto idTarget = c->GetId ();
  c->SetPosition (NOT_SAFE_FURTHER);
  NoAttacks (*c);
  SetHp (*c, 0, 10, 0, 10);
  c.reset ();

  FindTargetsAndDamage ();

  c = characters.GetById (idSafe);
  EXPECT_EQ (c->GetHP ().armour (), 10);
  EXPECT_EQ (c->GetEffects ().speed ().percent (), 0);

  c = characters.GetById (idTarget);
  EXPECT_EQ (c->GetHP ().armour (), 8);
  EXPECT_EQ (c->GetEffects ().speed ().percent (), -50);
}

TEST_F (DealDamageTests, RandomisedDamage)
{
  constexpr unsigned minDmg = 5;
  constexpr unsigned maxDmg = 10;
  constexpr unsigned rolls = 1000;
  constexpr unsigned threshold = rolls / (maxDmg - minDmg + 1) * 80 / 100;

  auto c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (*c, 1, minDmg, maxDmg);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  NoAttacks (*c);
  SetHp (*c, 0, maxDmg * rolls, 0, maxDmg * rolls);
  c.reset ();

  std::vector<unsigned> cnts(maxDmg + 1);
  for (unsigned i = 0; i < rolls; ++i)
    {
      const int before = characters.GetById (idTarget)->GetHP ().armour ();
      FindTargetsAndDamage ();
      const int after = characters.GetById (idTarget)->GetHP ().armour ();

      const int dmgDone = before - after;
      ASSERT_GE (dmgDone, minDmg);
      ASSERT_LE (dmgDone, maxDmg);

      ++cnts[dmgDone];
    }

  for (unsigned i = minDmg; i <= maxDmg; ++i)
    {
      LOG (INFO) << "Damage " << i << " done: " << cnts[i] << " times";
      EXPECT_GE (cnts[i], threshold);
    }
}

TEST_F (DealDamageTests, HpReduction)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idAttacker = c->GetId ();
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  NoAttacks (*c);
  c.reset ();

  struct TestCase
  {
    unsigned dmg;
    unsigned hpBeforeShield;
    unsigned hpBeforeArmour;
    unsigned hpAfterShield;
    unsigned hpAfterArmour;
  };
  const TestCase tests[] = {
    {0, 5, 5, 5, 5},
    {1, 1, 10, 0, 10},
    {1, 0, 10, 0, 9},
    {2, 1, 10, 0, 9},
    {2, 0, 1, 0, 0},
    {3, 1, 1, 0, 0},
    {1, 0, 0, 0, 0},
  };

  for (const auto t : tests)
    {
      c = characters.GetById (idAttacker);
      c->MutableProto ().clear_combat_data ();
      AddAttack (*c, 1, t.dmg, t.dmg);
      c.reset ();

      c = characters.GetById (idTarget);
      c->MutableHP ().set_shield_mhp (999);
      c->MutableHP ().set_shield (t.hpBeforeShield);
      c->MutableHP ().set_armour (t.hpBeforeArmour);
      c.reset ();

      FindTargetsAndDamage ();

      c = characters.GetById (idTarget);
      EXPECT_GE (c->GetHP ().shield_mhp (), 999);
      EXPECT_GE (c->GetHP ().shield (), t.hpAfterShield);
      EXPECT_GE (c->GetHP ().armour (), t.hpAfterArmour);

      EXPECT_EQ (c->GetHP ().shield (), t.hpAfterShield);
      EXPECT_EQ (c->GetHP ().armour (), t.hpAfterArmour);
    }
}

TEST_F (DealDamageTests, Kills)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (*c, 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (*c, 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  c->SetPosition (HexCoord (10, 10));
  AddAttack (*c, 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id1 = c->GetId ();
  NoAttacks (*c);
  SetHp (*c, 0, 1, 1, 1);
  c->MutableHP ().set_shield_mhp (999);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (10, 10));
  NoAttacks (*c);
  SetHp (*c, 1, 1, 1, 1);
  c.reset ();

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAre (
    TargetKey (proto::TargetId::TYPE_CHARACTER, id1)
  ));

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAre (
    TargetKey (proto::TargetId::TYPE_CHARACTER, id2)
  ));
}

TEST_F (DealDamageTests, Effects)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  AddAttack (*c, 5, 1, 1);
  auto& attack = CombatTests::AddAttack (*c);
  attack.set_range (5);
  attack.mutable_effects ()->mutable_speed ()->set_percent (-10);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->MutableEffects ().mutable_speed ()->set_percent (50);
  NoAttacks (*c);
  c.reset ();

  FindTargetsAndDamage ();

  c = characters.GetById (idTarget);
  EXPECT_EQ (c->GetEffects ().speed ().percent (), 40);
}

TEST_F (DealDamageTests, EffectsAndDamageApplied)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  AddAttack (*c, 5, 1, 1);
  {
    auto& attack = CombatTests::AddAttack (*c);
    attack.set_range (5);
    attack.mutable_effects ()->mutable_speed ()->set_percent (-10);
  }
  {
    auto& attack = CombatTests::AddAttack (*c);
    attack.set_area (5);
    attack.mutable_effects ()->mutable_speed ()->set_percent (-5);
  }
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTarget = c->GetId ();
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  FindTargetsAndDamage ();

  c = characters.GetById (idTarget);
  EXPECT_EQ (c->GetHP ().armour (), 99);
  EXPECT_EQ (c->GetEffects ().speed ().percent (), -15);
}

/* ************************************************************************** */

using LowHpBoostTests = DealDamageTests;

TEST_F (LowHpBoostTests, RangeAndDamage)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  SetHp (*c, 0, 10, 0, 100);
  AddAreaAttack (*c, 2, 1, 1).set_range (5);
  AddLowHpBoost (*c, 10, 100);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idArea = c->GetId ();
  c->SetPosition (HexCoord (10, 4));
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  FindTargetsAndDamage ();

  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 98);
  EXPECT_EQ (characters.GetById (idArea)->GetHP ().armour (), 98);
}

TEST_F (LowHpBoostTests, Stacking)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  SetHp (*c, 0, 10, 0, 100);
  AddAttack (*c, 5, 1, 1);
  /* This will give a total boost of 300% (4x) to range and damage.  The last
     of the boosts is not in effect.  */
  AddLowHpBoost (*c, 10, 100);
  AddLowHpBoost (*c, 10, 100);
  AddLowHpBoost (*c, 20, 100);
  AddLowHpBoost (*c, 9, 100);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (20, 0));
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 96);
}

TEST_F (LowHpBoostTests, BasedOnOriginalHp)
{
  /* Two characters are attacking each other.  The low-HP boost should
     be determined based on the original HP before applying any damage,
     so neither of them should get any boost from the current damage round.  */

  auto c = characters.CreateNew ("red", Faction::RED);
  const auto id1 = c->GetId ();
  SetHp (*c, 0, 11, 0, 100);
  AddAttack (*c, 5, 1, 1);
  AddLowHpBoost (*c, 10, 100);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (5, 0));
  SetHp (*c, 0, 11, 0, 100);
  AddAttack (*c, 5, 1, 1);
  AddLowHpBoost (*c, 10, 100);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (id1)->GetHP ().armour (), 10);
  EXPECT_EQ (characters.GetById (id2)->GetHP ().armour (), 10);

  /* Now both get the boost.  */
  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (id1)->GetHP ().armour (), 8);
  EXPECT_EQ (characters.GetById (id2)->GetHP ().armour (), 8);
}

/* ************************************************************************** */

using SelfDestructTests = DealDamageTests;

TEST_F (SelfDestructTests, Basic)
{
  /* This sets up a basic situation with three characters:  One kills
     the second, which self-destructs and inflicts damage back onto the
     first.  We also have a third character, which we use to check that the
     first character, which has self-destruct but is not killed, does not
     apply extra self-destruct damage.  */

  auto c = characters.CreateNew ("red", Faction::RED);
  const auto idAlive = c->GetId ();
  SetHp (*c, 0, 100, 0, 100);
  AddAttack (*c, 5, 10, 10);
  AddSelfDestruct (*c, 10, 80);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idDestructed = c->GetId ();
  c->SetPosition (HexCoord (5, 0));
  SetHp (*c, 0, 10, 0, 10);
  AddSelfDestruct (*c, 5, 30);
  c.reset ();

  c = characters.CreateNew ("blue", Faction::BLUE);
  const auto idStandby = c->GetId ();
  c->SetPosition (HexCoord (-6, 0));
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAre (
    TargetKey (proto::TargetId::TYPE_CHARACTER, idDestructed)
  ));
  EXPECT_EQ (characters.GetById (idAlive)->GetHP ().armour (), 70);
  EXPECT_EQ (characters.GetById (idStandby)->GetHP ().armour (), 100);
}

TEST_F (SelfDestructTests, StackingAndLowHpBoost)
{
  /* Even if a character is "one-shot" killed (had full HP before),
     the low-HP boost should apply to its self-destruct.  */

  auto c = characters.CreateNew ("red", Faction::RED);
  const auto idAttacker = c->GetId ();
  SetHp (*c, 0, 100, 0, 100);
  AddAttack (*c, 100, 1, 1);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idDestructed = c->GetId ();
  c->SetPosition (HexCoord (12, 0));
  SetHp (*c, 0, 1, 0, 1);
  AddSelfDestruct (*c, 10, 10);
  AddSelfDestruct (*c, 10, 10);
  AddLowHpBoost (*c, 1, 10);
  AddLowHpBoost (*c, 0, 10);
  c.reset ();

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAre (
    TargetKey (proto::TargetId::TYPE_CHARACTER, idDestructed)
  ));
  EXPECT_EQ (characters.GetById (idAttacker)->GetHP ().armour (), 100 - 24);
}

TEST_F (SelfDestructTests, Chain)
{
  constexpr int length = 100;
  static_assert (length % 2 == 0, "length should be even");

  auto c = characters.CreateNew ("red", Faction::RED);
  const auto idTrigger = c->GetId ();
  SetHp (*c, 0, 100, 0, 100);
  AddAttack (*c, 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("red", Faction::RED);
  const auto idEnd = c->GetId ();
  c->SetPosition (HexCoord (length + 1, 0));
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  std::vector<TargetKey> expectedDead;
  for (int i = 1; i <= length; i += 2)
    {
      /* We add a pair of blue/green characters to the chain at each step.
         They are created in reversed order to their position, so that
         we can also verify that the returned kills are sorted by TargetKey's
         order and not by time.  */

      c = characters.CreateNew ("green", Faction::GREEN);
      const auto idLow = c->GetId ();
      c->SetPosition (HexCoord (i + 1, 0));
      SetHp (*c, 0, 1, 0, 1);
      AddSelfDestruct (*c, 1, 1);
      c.reset ();

      c = characters.CreateNew ("blue", Faction::BLUE);
      const auto idHigh = c->GetId ();
      c->SetPosition (HexCoord (i, 0));
      SetHp (*c, 0, 1, 0, 1);
      AddSelfDestruct (*c, 1, 1);
      c.reset ();

      expectedDead.emplace_back (proto::TargetId::TYPE_CHARACTER, idLow);
      expectedDead.emplace_back (proto::TargetId::TYPE_CHARACTER, idHigh);
    }

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAreArray (expectedDead));
  EXPECT_EQ (characters.GetById (idTrigger)->GetHP ().armour (), 99);
  EXPECT_EQ (characters.GetById (idEnd)->GetHP ().armour (), 99);
}

TEST_F (SelfDestructTests, SafeZone)
{
  /* One character kills another to trigger self-destruct (both are not
     in a safe zone).  The self-destruct should then hit back the
     attacker, but should not affect another one close by in the safe zone.  */

  auto c = characters.CreateNew ("red", Faction::RED);
  const auto idAlive = c->GetId ();
  c->SetPosition (NOT_SAFE);
  AddAttack (*c, 10, 5, 5);
  SetHp (*c, 0, 100, 0, 100);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idDestructed = c->GetId ();
  c->SetPosition (NOT_SAFE_FURTHER);
  SetHp (*c, 0, 1, 0, 1);
  AddSelfDestruct (*c, 10, 10);
  c.reset ();

  c = characters.CreateNew ("blue", Faction::BLUE);
  const auto idSafe = c->GetId ();
  c->SetPosition (SAFE);
  SetHp (*c, 0, 100, 0, 100);
  NoAttacks (*c);
  c.reset ();

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAre (
    TargetKey (proto::TargetId::TYPE_CHARACTER, idDestructed)
  ));
  EXPECT_EQ (characters.GetById (idAlive)->GetHP ().armour (), 90);
  EXPECT_EQ (characters.GetById (idSafe)->GetHP ().armour (), 100);
}

/* ************************************************************************** */

using DamageListTests = DealDamageTests;

TEST_F (DamageListTests, Basic)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idAttacker = c->GetId ();
  AddAttack (*c, 2, 1, 1);
  c.reset ();

  /* This character has no attack in range, so should not be put onto
     the damage list.  */
  c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (*c, 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (2, 0));
  NoAttacks (*c);
  SetHp (*c, 0, 10, 0, 10);
  c.reset ();

  /* Add an existing dummy entry to verify it is kept.  */
  dl.AddEntry (idTarget, 42);

  FindTargetsAndDamage ();
  EXPECT_EQ (dl.GetAttackers (idTarget),
             DamageLists::Attackers ({42, idAttacker}));
}

TEST_F (DamageListTests, ReciprocalKill)
{
  /* When two characters kill each other in one shot at the same time,
     both should end up on each other's damage list.  */

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  AddAttack (*c, 1, 1, 1);
  SetHp (*c, 0, 1, 0, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  AddAttack (*c, 1, 1, 1);
  SetHp (*c, 0, 1, 0, 1);
  c.reset ();

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAre (
    TargetKey (proto::TargetId::TYPE_CHARACTER, id1),
    TargetKey (proto::TargetId::TYPE_CHARACTER, id2)
  ));
  EXPECT_EQ (dl.GetAttackers (id1), DamageLists::Attackers ({id2}));
  EXPECT_EQ (dl.GetAttackers (id2), DamageLists::Attackers ({id1}));
}

TEST_F (DamageListTests, MultipleKillers)
{
  /* Even if some character is already dead from processing another attacker's
     damage, later attackers (except in later self-destruct rounds) should still
     be tracked on the damage list.  */

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idTarget = c->GetId ();
  NoAttacks (*c);
  SetHp (*c, 0, 1, 0, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idAttacker1 = c->GetId ();
  AddAttack (*c, 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::BLUE);
  const auto idAttacker2 = c->GetId ();
  AddAttack (*c, 1, 1, 1);
  c.reset ();

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAre (
    TargetKey (proto::TargetId::TYPE_CHARACTER, idTarget)
  ));
  EXPECT_EQ (dl.GetAttackers (idTarget),
             DamageLists::Attackers ({idAttacker1, idAttacker2}));
}

TEST_F (DamageListTests, WithSelfDestruct)
{
  /* Damage from self-destructs should be credited to the destructed character.
     But if the self-destruct is triggered by another self-destruct, then
     the already-dead character should not be credited to the later
     self-destructor.  */

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idTrigger = c->GetId ();
  AddAttack (*c, 10, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idDestruct1 = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  AddSelfDestruct (*c, 5, 1);
  SetHp (*c, 0, 1, 0, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::BLUE);
  const auto idDestruct2 = c->GetId ();
  c->SetPosition (HexCoord (15, 0));
  AddSelfDestruct (*c, 5, 1);
  SetHp (*c, 0, 1, 0, 1);
  c.reset ();

  EXPECT_THAT (FindTargetsAndDamage (), ElementsAre (
    TargetKey (proto::TargetId::TYPE_CHARACTER, idDestruct1),
    TargetKey (proto::TargetId::TYPE_CHARACTER, idDestruct2)
  ));
  EXPECT_EQ (dl.GetAttackers (idDestruct1),
             DamageLists::Attackers ({idTrigger}));
  EXPECT_EQ (dl.GetAttackers (idDestruct2),
             DamageLists::Attackers ({idDestruct1}));
}

/* ************************************************************************** */

class ProcessKillsTests : public CombatTests
{

protected:

  GroundLootTable loot;
  OngoingsTable ongoings;

  ProcessKillsTests ()
    : loot(db), ongoings(db)
  {}

};

class ProcessKillsCharacterTests : public ProcessKillsTests
{

protected:

  void
  KillCharacter (const Database::IdT id)
  {
    proto::TargetId targetId;
    targetId.set_type (proto::TargetId::TYPE_CHARACTER);
    targetId.set_id (id);
    ProcessKills (db, dl, loot, {targetId}, rnd, ctx);
  }

};

TEST_F (ProcessKillsCharacterTests, DeletesCharacters)
{
  const auto id1 = characters.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = characters.CreateNew ("domob", Faction::RED)->GetId ();

  ProcessKills (db, dl, loot, {}, rnd, ctx);
  EXPECT_TRUE (characters.GetById (id1) != nullptr);
  EXPECT_TRUE (characters.GetById (id2) != nullptr);

  KillCharacter (id2);

  EXPECT_TRUE (characters.GetById (id1) != nullptr);
  EXPECT_TRUE (characters.GetById (id2) == nullptr);
}

TEST_F (ProcessKillsCharacterTests, RemovesFromDamageLists)
{
  const auto id1 = characters.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = characters.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id3 = characters.CreateNew ("domob", Faction::RED)->GetId ();

  DamageLists dl(db, 0);
  dl.AddEntry (id1, id2);
  dl.AddEntry (id1, id3);
  dl.AddEntry (id2, id1);

  KillCharacter (id2);

  EXPECT_EQ (dl.GetAttackers (id1), DamageLists::Attackers ({id3}));
  EXPECT_EQ (dl.GetAttackers (id2), DamageLists::Attackers ({}));
}

TEST_F (ProcessKillsCharacterTests, RemovesOngoings)
{
  const auto id1 = characters.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = characters.CreateNew ("domob", Faction::RED)->GetId ();

  db.SetNextId (101);
  ongoings.CreateNew (1)->SetCharacterId (id1);
  ongoings.CreateNew (1)->SetCharacterId (id2);
  ongoings.CreateNew (1)->SetBuildingId (12345);

  KillCharacter (id2);

  EXPECT_NE (ongoings.GetById (101), nullptr);
  EXPECT_EQ (ongoings.GetById (102), nullptr);
  EXPECT_NE (ongoings.GetById (103), nullptr);
}

TEST_F (ProcessKillsCharacterTests, CancelsProspection)
{
  const HexCoord pos(-42, 100);
  const auto regionId = ctx.Map ().Regions ().GetRegionId (pos);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (pos);

  auto op = ongoings.CreateNew (1);
  const auto opId = op->GetId ();
  c->MutableProto ().set_ongoing (op->GetId ());
  op->SetCharacterId (id);
  op->MutableProto ().mutable_prospection ();

  op.reset ();
  c.reset ();

  ctx.SetHeight (1'042);
  RegionsTable regions(db, ctx.Height ());
  auto r = regions.GetById (regionId);
  r->MutableProto ().set_prospecting_character (id);
  r.reset ();

  KillCharacter (id);

  EXPECT_TRUE (characters.GetById (id) == nullptr);
  r = regions.GetById (regionId);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_TRUE (ongoings.GetById (opId) == nullptr);
}

TEST_F (ProcessKillsCharacterTests, DropsInventory)
{
  const HexCoord pos(-42, 100);
  loot.GetByCoord (pos)->GetInventory ().SetFungibleCount ("foo", 5);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->MutableProto ().set_cargo_space (1000);
  c->SetPosition (pos);
  c->GetInventory ().SetFungibleCount ("foo", 2);
  c->GetInventory ().SetFungibleCount ("bar", 10);
  c.reset ();

  KillCharacter (id);

  EXPECT_TRUE (characters.GetById (id) == nullptr);
  auto ground = loot.GetByCoord (pos);
  EXPECT_EQ (ground->GetInventory ().GetFungibleCount ("foo"), 7);
  EXPECT_EQ (ground->GetInventory ().GetFungibleCount ("bar"), 10);
}

class ProcessKillsBuildingTests : public ProcessKillsTests
{

protected:

  void
  KillBuilding (const Database::IdT id)
  {
    proto::TargetId targetId;
    targetId.set_type (proto::TargetId::TYPE_BUILDING);
    targetId.set_id (id);
    ProcessKills (db, dl, loot, {targetId}, rnd, ctx);
  }

};

TEST_F (ProcessKillsBuildingTests, RemovesBuildingAndInventories)
{
  const auto id1
      = buildings.CreateNew ("checkmark", "", Faction::ANCIENT)->GetId ();
  const auto id2
      = buildings.CreateNew ("checkmark", "domob", Faction::RED)->GetId ();

  inventories.Get (id1, "domob")->GetInventory ().AddFungibleCount ("foo", 10);
  inventories.Get (id2, "domob")->GetInventory ().AddFungibleCount ("foo", 20);

  KillBuilding (id2);

  ASSERT_EQ (buildings.GetById (id2), nullptr);
  auto b = buildings.GetById (id1);
  ASSERT_NE (b, nullptr);
  EXPECT_EQ (b->GetId (), id1);

  auto res = inventories.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto i = inventories.GetFromResult (res);
  EXPECT_EQ (i->GetBuildingId (), id1);
  EXPECT_EQ (i->GetAccount (), "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("foo"), 10);
  EXPECT_FALSE (res.Step ());
}

TEST_F (ProcessKillsBuildingTests, RemovesOngoings)
{
  const auto bId
      = buildings.CreateNew ("checkmark", "domob", Faction::RED)->GetId ();

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto cId = c->GetId ();
  c->SetBuildingId (bId);
  c.reset ();

  db.SetNextId (101);
  ongoings.CreateNew (1)->SetHeight (42);
  ongoings.CreateNew (1)->SetBuildingId (bId);
  ongoings.CreateNew (1)->SetCharacterId (cId);
  ongoings.CreateNew (1)->SetBuildingId (12345);

  KillBuilding (bId);

  EXPECT_NE (ongoings.GetById (101), nullptr);
  EXPECT_EQ (ongoings.GetById (102), nullptr);
  EXPECT_EQ (ongoings.GetById (103), nullptr);
  EXPECT_NE (ongoings.GetById (104), nullptr);
}

TEST_F (ProcessKillsBuildingTests, MayDropAnyInventoryItem)
{
  /* In this test, we verify that any inventory item inside the building
     (both from account inventories and held by characters in the building)
     may be dropped when the building is destroyed.  We do this by destroying
     the building many times and building the "union" of dropped items.  For
     enough trials, this will give us the full set of all items inside.

     We also verify that if something is dropped, it will be the total
     amount of this item inside the building (or otherwise nothing).  */

  constexpr unsigned trials = 100;
  const HexCoord pos(10, 20);

  const std::map<std::string, Quantity> expectedAmounts =
    {
      {"foo", 5},
      {"bar", 100},
      {"zerospace", 1},
    };

  std::set<std::string> dropped;
  for (unsigned i = 0; i < trials; ++i)
    {
      auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
      const auto id = b->GetId ();
      b->SetCentre (pos);
      b.reset ();

      inventories.Get (id, "a")->GetInventory ().SetFungibleCount ("foo", 1);
      inventories.Get (id, "b")->GetInventory ().SetFungibleCount ("foo", 2);
      inventories.Get (id, "b")->GetInventory ().SetFungibleCount ("bar", 100);

      auto c = characters.CreateNew ("domob", Faction::RED);
      c->SetBuildingId (id);
      c->GetInventory ().SetFungibleCount ("foo", 2);
      c.reset ();

      c = characters.CreateNew ("andy", Faction::RED);
      c->SetBuildingId (id);
      c->GetInventory ().SetFungibleCount ("zerospace", 1);
      c.reset ();

      KillBuilding (id);

      auto l = loot.GetByCoord (pos);
      for (const auto& entry : l->GetInventory ().GetFungible ())
        {
          EXPECT_EQ (entry.second, expectedAmounts.at (entry.first));
          dropped.insert (entry.first);
        }
      l->GetInventory ().Clear ();
    }

  EXPECT_EQ (dropped.size (), expectedAmounts.size ());
}

TEST_F (ProcessKillsBuildingTests, MayDropConstructionInventory)
{
  const HexCoord pos(10, 20);
  constexpr unsigned trials = 100;
  unsigned dropped = 0;

  for (unsigned i = 0; i < trials; ++i)
    {
      auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
      const auto bId = b->GetId ();
      b->SetCentre (pos);
      b->MutableProto ().set_foundation (true);
      Inventory cInv(*b->MutableProto ().mutable_construction_inventory ());
      cInv.AddFungibleCount ("foo", 1);
      b.reset ();

      KillBuilding (bId);

      auto l = loot.GetByCoord (pos);
      const auto cnt = l->GetInventory ().GetFungibleCount ("foo");
      EXPECT_LE (cnt, 1);
      if (cnt == 1)
        ++dropped;
      l->GetInventory ().Clear ();
    }

  LOG (INFO) << "Copied blueprint dropped " << dropped << " times";
  EXPECT_GT (dropped, 0);
}

TEST_F (ProcessKillsBuildingTests, MayDropCopiedBlueprint)
{
  const HexCoord pos(10, 20);
  constexpr unsigned trials = 100;
  unsigned dropped = 0;

  for (unsigned i = 0; i < trials; ++i)
    {
      auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
      const auto bId = b->GetId ();
      b->SetCentre (pos);
      b.reset ();

      auto op = ongoings.CreateNew (1);
      op->SetHeight (42);
      op->SetBuildingId (bId);
      auto& cp = *op->MutableProto ().mutable_blueprint_copy ();
      cp.set_account ("domob");
      cp.set_original_type ("bow bpo");
      cp.set_copy_type ("bow bpc");
      cp.set_num_copies (42);
      op.reset ();

      KillBuilding (bId);

      auto l = loot.GetByCoord (pos);
      EXPECT_EQ (l->GetInventory ().GetFungibleCount ("bow bpc"), 0);
      const auto originalCnt = l->GetInventory ().GetFungibleCount ("bow bpo");
      EXPECT_LE (originalCnt, 1);
      if (originalCnt == 1)
        ++dropped;
      l->GetInventory ().Clear ();
    }

  LOG (INFO) << "Copied blueprint dropped " << dropped << " times";
  EXPECT_GT (dropped, 0);
}

TEST_F (ProcessKillsBuildingTests, MayDropBlueprintsFromConstruction)
{
  const HexCoord pos(10, 20);
  constexpr unsigned trials = 100;
  unsigned dropped = 0;

  for (unsigned i = 0; i < trials; ++i)
    {
      auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
      const auto bId = b->GetId ();
      b->SetCentre (pos);
      b.reset ();

      auto op = ongoings.CreateNew (1);
      op->SetHeight (42);
      op->SetBuildingId (bId);
      auto* c = op->MutableProto ().mutable_item_construction ();
      c->set_account ("domob");
      c->set_output_type ("bow");
      c->set_num_items (42);
      c->set_original_type ("bow bpo");
      op.reset ();

      op = ongoings.CreateNew (1);
      op->SetHeight (42);
      op->SetBuildingId (bId);
      c = op->MutableProto ().mutable_item_construction ();
      c->set_account ("domob");
      c->set_output_type ("sword");
      c->set_num_items (10);
      op.reset ();

      KillBuilding (bId);

      auto l = loot.GetByCoord (pos);
      const auto originalCnt = l->GetInventory ().GetFungibleCount ("bow bpo");
      EXPECT_LE (originalCnt, 1);
      if (originalCnt == 1)
        ++dropped;
      l->GetInventory ().SetFungibleCount ("bow bpo", 0);

      /* Nothing else should have been dropped.  */
      ASSERT_TRUE (l->GetInventory ().IsEmpty ());
    }

  LOG (INFO) << "Construction blueprint dropped " << dropped << " times";
  EXPECT_GT (dropped, 0);
}

TEST_F (ProcessKillsBuildingTests, ItemDropChance)
{
  /* This verifies that the chance for dropping an item from a destroyed
     building is roughly what we expect it to be.  */

  constexpr unsigned trials = 1'000;
  constexpr unsigned expected = (trials * 30) / 100;
  constexpr unsigned eps = (trials * 5) / 100;

  const HexCoord pos(10, 20);
  for (unsigned i = 0; i < trials; ++i)
    {
      auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
      const auto id = b->GetId ();
      b->SetCentre (pos);
      b.reset ();

      inventories.Get (id, "x")->GetInventory ().SetFungibleCount ("foo", 1);
      KillBuilding (id);
    }

  auto l = loot.GetByCoord (pos);
  const auto cnt = l->GetInventory ().GetFungibleCount ("foo");
  EXPECT_GE (cnt, expected - eps);
  EXPECT_LE (cnt, expected + eps);
}

TEST_F (ProcessKillsBuildingTests, OrderOfItemRolls)
{
  /* This test verifies that the order in which random rolls for dropping
     items are done matches the expected order (increasing item name
     as a string).  For this, we just explicitly repeat the rolls in the
     expected order, and check the outcome against that.  */

  constexpr unsigned trials = 1'000;
  const std::vector<std::string> items =
    {
      "raw a",
      "raw b",
      "raw c",
      "raw d",
      "raw e",
      "raw f",
      "raw g",
      "raw h",
      "raw i",
    };
  const HexCoord pos(10, 20);

  for (unsigned i = 0; i < trials; ++i)
    {
      auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
      const auto id = b->GetId ();
      b->SetCentre (pos);
      b.reset ();

      inventories.Get (id, "z")->GetInventory ().SetFungibleCount ("raw a", 1);
      inventories.Get (id, "z")->GetInventory ().SetFungibleCount ("raw i", 1);
      inventories.Get (id, "a")->GetInventory ().SetFungibleCount ("raw h", 1);
      inventories.Get (id, "a")->GetInventory ().SetFungibleCount ("raw b", 1);

      auto c = characters.CreateNew ("domob", Faction::RED);
      c->SetBuildingId (id);
      auto& inv = c->GetInventory ();
      inv.SetFungibleCount ("raw c", 1);
      inv.SetFungibleCount ("raw d", 1);
      inv.SetFungibleCount ("raw e", 1);
      inv.SetFungibleCount ("raw f", 1);
      inv.SetFungibleCount ("raw g", 1);
      c.reset ();

      /* Use a custom seed for randomness so that we can replay the
         exact same sequence.  */
      std::ostringstream seedStr;
      seedStr << "seed " << i;
      const xaya::uint256 seed = xaya::SHA256::Hash (seedStr.str ());

      rnd.Seed (seed);
      KillBuilding (id);

      auto l = loot.GetByCoord (pos);
      auto& dropped = l->GetInventory ();

      rnd.Seed (seed);
      for (const auto& item : items)
        if (rnd.ProbabilityRoll (30, 100))
          {
            ASSERT_EQ (dropped.GetFungibleCount (item), 1);
            dropped.SetFungibleCount (item, 0);
          }
        else
          ASSERT_EQ (dropped.GetFungibleCount (item), 0);
    }
}

/* ************************************************************************** */

using RegenerateHpTests = CombatTests;

TEST_F (RegenerateHpTests, Works)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  auto* regen = &c->MutableRegenData ();
  regen->mutable_max_hp ()->set_shield (100);
  c.reset ();

  struct TestCase
  {
    unsigned mhpRegen;
    unsigned mhpShieldBefore;
    unsigned shieldBefore;
    unsigned mhpShieldAfter;
    unsigned shieldAfter;
  };
  const TestCase tests[] = {
    {0, 100, 50, 100, 50},
    {500, 0, 50, 500, 50},
    {500, 500, 50, 0, 51},
    {750, 750, 50, 500, 51},
    {2000, 0, 50, 0, 52},
    {500, 900, 99, 0, 100},
    {100, 0, 100, 0, 100},
    {2000, 999, 99, 0, 100},
  };

  for (const auto& t : tests)
    {
      c = characters.GetById (id);
      c->MutableHP ().set_shield (t.shieldBefore);
      c->MutableHP ().set_shield_mhp (t.mhpShieldBefore);
      regen = &c->MutableRegenData ();
      regen->set_shield_regeneration_mhp (t.mhpRegen);
      c.reset ();

      RegenerateHP (db);

      c = characters.GetById (id);
      EXPECT_EQ (c->GetHP ().shield (), t.shieldAfter);
      EXPECT_EQ (c->GetHP ().shield_mhp (), t.mhpShieldAfter);
    }
}

TEST_F (RegenerateHpTests, BuildingsRegenerate)
{
  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto id = b->GetId ();
  b->MutableHP ().set_shield (10);
  auto& regen = b->MutableRegenData ();
  regen.mutable_max_hp ()->set_shield (100);
  regen.set_shield_regeneration_mhp (1000);
  b.reset ();

  RegenerateHP (db);

  b = buildings.GetById (id);
  EXPECT_EQ (b->GetHP ().shield (), 11);
}

TEST_F (RegenerateHpTests, InsideBuilding)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetBuildingId (100);
  c->MutableHP ().set_shield (10);
  auto* regen = &c->MutableRegenData ();
  regen->mutable_max_hp ()->set_shield (100);
  regen->set_shield_regeneration_mhp (1000);
  c.reset ();

  RegenerateHP (db);

  c = characters.GetById (id);
  EXPECT_EQ (c->GetHP ().shield (), 11);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
