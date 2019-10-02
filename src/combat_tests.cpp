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
#include "database/region.hpp"
#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <xayautil/hash.hpp>
#include <xayautil/random.hpp>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <map>
#include <vector>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class CombatTests : public DBTestWithSchema
{

protected:

  const BaseMap map;
  CharacterTable characters;
  DamageLists dl;
  xaya::Random rnd;

  CombatTests ()
    : characters(db), dl(db, 0)
  {
    xaya::SHA256 seed;
    seed << "random seed";
    rnd.Seed (seed.Finalise ());
  }

  /**
   * Adds an attack with the given range to the character proto.
   */
  static proto::Attack&
  AddAttackWithRange (proto::Character& pb, const HexCoord::IntT range)
  {
    auto* attack = pb.mutable_combat_data ()->add_attacks ();
    attack->set_range (range);
    return *attack;
  }

  /**
   * Initialises the combat data proto so that it is "valid" but has
   * no attacks.
   */
  static void
  NoAttacks (proto::Character& pb)
  {
    pb.mutable_combat_data ();
  }

};

/* ************************************************************************** */

using TargetSelectionTests = CombatTests;

TEST_F (TargetSelectionTests, NoTargets)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (-10, 0));
  c->MutableProto ().mutable_target ();
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob",Faction::RED);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (-10, 1));
  c->MutableProto ().mutable_target ();
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id3 = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  c->MutableProto ().mutable_target ();
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  FindCombatTargets (db, rnd);

  EXPECT_FALSE (characters.GetById (id1)->GetProto ().has_target ());
  EXPECT_FALSE (characters.GetById (id2)->GetProto ().has_target ());
  EXPECT_FALSE (characters.GetById (id3)->GetProto ().has_target ());
}

TEST_F (TargetSelectionTests, ClosestTarget)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  c->SetPosition (HexCoord (2, 2));
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (1, 1));
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  /* Since target selection is randomised, run this multiple times to ensure
     that we always pick the same target (single closest one).  */
  for (unsigned i = 0; i < 100; ++i)
    {
      FindCombatTargets (db, rnd);

      c = characters.GetById (idFighter);
      const auto& pb = c->GetProto ();
      ASSERT_TRUE (pb.has_target ());
      EXPECT_EQ (pb.target ().type (), proto::TargetId::TYPE_CHARACTER);
      EXPECT_EQ (pb.target ().id (), idTarget);
    }
}

TEST_F (TargetSelectionTests, MultipleAttacks)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttackWithRange (c->MutableProto (), 1);
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (7, 0));
  NoAttacks (c->MutableProto ());
  c.reset ();

  FindCombatTargets (db, rnd);

  c = characters.GetById (id1);
  const auto& pb = c->GetProto ();
  ASSERT_TRUE (pb.has_target ());
  EXPECT_EQ (pb.target ().type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (pb.target ().id (), id2);

  EXPECT_FALSE (characters.GetById (id2)->GetProto ().has_target ());
}

TEST_F (TargetSelectionTests, Randomisation)
{
  constexpr unsigned nTargets = 5;
  constexpr unsigned rolls = 1000;
  constexpr unsigned threshold = rolls / nTargets * 80 / 100;

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  std::map<Database::IdT, unsigned> targetMap;
  for (unsigned i = 0; i < nTargets; ++i)
    {
      c = characters.CreateNew ("domob", Faction::GREEN);
      targetMap.emplace (c->GetId (), i);
      c->SetPosition (HexCoord (1, 1));
      NoAttacks (c->MutableProto ());
      c.reset ();
    }
  ASSERT_EQ (targetMap.size (), nTargets);

  std::vector<unsigned> cnt(nTargets);
  for (unsigned i = 0; i < rolls; ++i)
    {
      FindCombatTargets (db, rnd);

      c = characters.GetById (idFighter);
      const auto& pb = c->GetProto ();
      ASSERT_TRUE (pb.has_target ());
      EXPECT_EQ (pb.target ().type (), proto::TargetId::TYPE_CHARACTER);

      const auto mit = targetMap.find (pb.target ().id ());
      ASSERT_NE (mit, targetMap.end ());
      ++cnt[mit->second];
    }

  for (unsigned i = 0; i < nTargets; ++i)
    {
      LOG (INFO) << "Target " << i << " was selected " << cnt[i] << " times";
      EXPECT_GE (cnt[i], threshold);
    }
}

/* ************************************************************************** */

class DealDamageTests : public CombatTests
{

protected:

  /**
   * Adds an attack with the given range and damage.
   */
  static void
  AddAttack (proto::Character& pb, const HexCoord::IntT range,
             const unsigned minDmg, const unsigned maxDmg)
  {
    auto& attack = AddAttackWithRange (pb, range);
    attack.set_min_damage (minDmg);
    attack.set_max_damage (maxDmg);
  }

  /**
   * Finds combat targets and deals damage.
   */
  std::vector<proto::TargetId>
  FindTargetsAndDamage ()
  {
    FindCombatTargets (db, rnd);
    return DealCombatDamage (db, dl, rnd);
  }

};

TEST_F (DealDamageTests, NoAttacks)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  NoAttacks (c->MutableProto ());
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (10);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 10);
}

TEST_F (DealDamageTests, OnlyAttacksInRange)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (c->MutableProto (), 1, 1, 1);
  AddAttack (c->MutableProto (), 2, 1, 1);
  AddAttack (c->MutableProto (), 3, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (2, 0));
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (10);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 8);
}

TEST_F (DealDamageTests, DamageLists)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto idAttacker = c->GetId ();
  AddAttack (c->MutableProto (), 2, 1, 1);
  c.reset ();

  /* This character has no attack in range, so should not be put onto
     the damage list.  */
  c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (c->MutableProto (), 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (2, 0));
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (10);
  c.reset ();

  /* Add an existing dummy entry to verify it is kept.  */
  dl.AddEntry (idTarget, 42);

  FindTargetsAndDamage ();
  EXPECT_EQ (dl.GetAttackers (idTarget),
             DamageLists::Attackers ({42, idAttacker}));
}

TEST_F (DealDamageTests, RandomisedDamage)
{
  constexpr unsigned minDmg = 5;
  constexpr unsigned maxDmg = 10;
  constexpr unsigned rolls = 1000;
  constexpr unsigned threshold = rolls / (maxDmg - minDmg + 1) * 80 / 100;

  auto c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (c->MutableProto (), 1, minDmg, maxDmg);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (maxDmg * rolls);
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
  NoAttacks (c->MutableProto ());
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
      AddAttack (c->MutableProto (), 1, t.dmg, t.dmg);
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
  AddAttack (c->MutableProto (), 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  AddAttack (c->MutableProto (), 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  c->SetPosition (HexCoord (10, 10));
  AddAttack (c->MutableProto (), 1, 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id1 = c->GetId ();
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_shield (0);
  c->MutableHP ().set_shield_mhp (999);
  c->MutableHP ().set_armour (1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (10, 10));
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_shield (1);
  c->MutableHP ().set_armour (1);
  c.reset ();

  auto dead = FindTargetsAndDamage ();
  ASSERT_EQ (dead.size (), 1);
  EXPECT_EQ (dead[0].type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (dead[0].id (), id1);

  dead = FindTargetsAndDamage ();
  ASSERT_EQ (dead.size (), 1);
  EXPECT_EQ (dead[0].type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (dead[0].id (), id2);
}

/* ************************************************************************** */

class ProcessKillsTests : public CombatTests
{

protected:

  GroundLootTable loot;

  ProcessKillsTests ()
    : loot(db)
  {}

};

TEST_F (ProcessKillsTests, DeletesCharacters)
{
  const auto id1 = characters.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = characters.CreateNew ("domob", Faction::RED)->GetId ();

  ProcessKills (db, dl, loot, {}, map);
  EXPECT_TRUE (characters.GetById (id1) != nullptr);
  EXPECT_TRUE (characters.GetById (id2) != nullptr);

  proto::TargetId targetId;
  targetId.set_type (proto::TargetId::TYPE_CHARACTER);
  targetId.set_id (id2);
  ProcessKills (db, dl, loot, {targetId}, map);

  EXPECT_TRUE (characters.GetById (id1) != nullptr);
  EXPECT_TRUE (characters.GetById (id2) == nullptr);
}

TEST_F (ProcessKillsTests, RemovesFromDamageLists)
{
  const auto id1 = characters.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = characters.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id3 = characters.CreateNew ("domob", Faction::RED)->GetId ();

  DamageLists dl(db, 0);
  dl.AddEntry (id1, id2);
  dl.AddEntry (id1, id3);
  dl.AddEntry (id2, id1);

  proto::TargetId targetId2;
  targetId2.set_type (proto::TargetId::TYPE_CHARACTER);
  targetId2.set_id (id2);
  ProcessKills (db, dl, loot, {targetId2}, map);

  EXPECT_EQ (dl.GetAttackers (id1), DamageLists::Attackers ({id3}));
  EXPECT_EQ (dl.GetAttackers (id2), DamageLists::Attackers ({}));
}

TEST_F (ProcessKillsTests, CancelsProspection)
{
  const HexCoord pos(-42, 100);
  const auto regionId = map.Regions ().GetRegionId (pos);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (pos);
  c->SetBusy (10);
  c->MutableProto ().mutable_prospection ();
  c.reset ();

  RegionsTable regions(db);
  auto r = regions.GetById (regionId);
  r->MutableProto ().set_prospecting_character (id);
  r.reset ();

  proto::TargetId targetId;
  targetId.set_type (proto::TargetId::TYPE_CHARACTER);
  targetId.set_id (id);
  ProcessKills (db, dl, loot, {targetId}, map);

  EXPECT_TRUE (characters.GetById (id) == nullptr);
  r = regions.GetById (regionId);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (ProcessKillsTests, DropsInventory)
{
  const HexCoord pos(-42, 100);
  loot.GetByCoord (pos)->GetInventory ().SetFungibleCount ("foo", 5);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (pos);
  c->GetInventory ().SetFungibleCount ("foo", 2);
  c->GetInventory ().SetFungibleCount ("bar", 10);
  c.reset ();

  proto::TargetId targetId;
  targetId.set_type (proto::TargetId::TYPE_CHARACTER);
  targetId.set_id (id);
  ProcessKills (db, dl, loot, {targetId}, map);

  EXPECT_TRUE (characters.GetById (id) == nullptr);
  auto ground = loot.GetByCoord (pos);
  EXPECT_EQ (ground->GetInventory ().GetFungibleCount ("foo"), 7);
  EXPECT_EQ (ground->GetInventory ().GetFungibleCount ("bar"), 10);
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

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
