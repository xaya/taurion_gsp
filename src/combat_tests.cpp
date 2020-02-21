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

  ContextForTesting ctx;

  BuildingsTable buildings;
  BuildingInventoriesTable inventories;

  CharacterTable characters;
  DamageLists dl;
  TestRandom rnd;

  CombatTests ()
    : buildings(db), inventories(db), characters(db), dl(db, 0)
  {}

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
  c->MutableTarget ().set_id (42);
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob",Faction::RED);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (-10, 1));
  c->MutableTarget ().set_id (42);
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id3 = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  c->MutableTarget ().set_id (42);
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  FindCombatTargets (db, rnd);

  EXPECT_FALSE (characters.GetById (id1)->GetTarget ().has_id ());
  EXPECT_FALSE (characters.GetById (id2)->GetTarget ().has_id ());
  EXPECT_FALSE (characters.GetById (id3)->GetTarget ().has_id ());
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
      const auto& t = c->GetTarget ();
      ASSERT_TRUE (t.has_id ());
      EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
      EXPECT_EQ (t.id (), idTarget);
    }
}

TEST_F (TargetSelectionTests, InsideBuildings)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  c->MutableTarget ().set_id (42);
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob",Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetBuildingId (100);
  /* This character will not be processed for target finding, so an existing
     target will not actually be cleared.  (But a new one should also not be
     added to it.)  We clear the target when the character enters a
     building.  */
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  FindCombatTargets (db, rnd);

  EXPECT_FALSE (characters.GetById (id1)->GetTarget ().has_id ());
  EXPECT_FALSE (characters.GetById (id2)->GetTarget ().has_id ());
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
  const auto& t = c->GetTarget ();
  ASSERT_TRUE (t.has_id ());
  EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (t.id (), id2);

  EXPECT_FALSE (characters.GetById (id2)->GetTarget ().has_id ());
}

TEST_F (TargetSelectionTests, OnlyAreaAttacks)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttackWithRange (c->MutableProto (), 7).set_area (true);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (7, 0));
  AddAttackWithRange (c->MutableProto (), 6).set_area (true);
  c.reset ();

  FindCombatTargets (db, rnd);

  c = characters.GetById (id1);
  const auto& t = c->GetTarget ();
  ASSERT_TRUE (t.has_id ());
  EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (t.id (), id2);

  EXPECT_FALSE (characters.GetById (id2)->GetTarget ().has_id ());
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
      const auto& t = c->GetTarget ();
      ASSERT_TRUE (t.has_id ());
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

/* ************************************************************************** */

class DealDamageTests : public CombatTests
{

protected:

  /**
   * Adds an attack with the given range and damage.  Returns a reference
   * to the added Attack proto so it can be further tweaked.
   */
  static proto::Attack&
  AddAttack (proto::Character& pb, const HexCoord::IntT range,
             const unsigned minDmg, const unsigned maxDmg)
  {
    auto& attack = AddAttackWithRange (pb, range);
    attack.set_min_damage (minDmg);
    attack.set_max_damage (maxDmg);
    return attack;
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

TEST_F (DealDamageTests, AreaAttacks)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  AddAttack (c->MutableProto (), 10, 1, 2).set_area (true);
  c.reset ();

  std::vector<Database::IdT> idTargets;
  for (unsigned i = 0; i < 10; ++i)
    {
      c = characters.CreateNew ("green", Faction::GREEN);
      idTargets.push_back (c->GetId ());
      NoAttacks (c->MutableProto ());
      c->MutableHP ().set_armour (1000);
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

TEST_F (DealDamageTests, MixedAttacks)
{
  auto c = characters.CreateNew ("red", Faction::RED);
  AddAttack (c->MutableProto (), 5, 1, 1).set_area (true);
  AddAttack (c->MutableProto (), 10, 1, 1);
  AddAttack (c->MutableProto (), 10, 1, 1).set_area (true);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTargetNear = c->GetId ();
  c->SetPosition (HexCoord (5, 0));
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (10);
  c.reset ();

  c = characters.CreateNew ("green", Faction::GREEN);
  const auto idTargetFar = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (10);
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

TEST_F (ProcessKillsCharacterTests, CancelsProspection)
{
  const HexCoord pos(-42, 100);
  const auto regionId = ctx.Map ().Regions ().GetRegionId (pos);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (pos);
  c->SetBusy (10);
  c->MutableProto ().mutable_prospection ();
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

  const std::map<std::string, Inventory::QuantityT> expectedAmounts =
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
          l->GetInventory ().SetFungibleCount (entry.first, 0);
        }
    }

  EXPECT_EQ (dropped.size (), expectedAmounts.size ());
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
