#include "combat.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <xayagame/hash.hpp>
#include <xayagame/random.hpp>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <map>
#include <sstream>
#include <vector>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class CombatTests : public DBTestWithSchema
{

protected:

  /** Character table for access to characters in the test.  */
  CharacterTable characters;

  /** Random instance for finding the targets.  */
  xaya::Random rnd;

  CombatTests ()
    : characters(db)
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
  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (-10, 0));
  c->MutableProto ().mutable_target ();
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", "bar", Faction::RED);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (-10, 1));
  c->MutableProto ().mutable_target ();
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", "baz", Faction::GREEN);
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
  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", "bar", Faction::GREEN);
  c->SetPosition (HexCoord (2, 2));
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", "baz", Faction::GREEN);
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
  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttackWithRange (c->MutableProto (), 1);
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  c = characters.CreateNew ("domob", "bar", Faction::GREEN);
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

  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  AddAttackWithRange (c->MutableProto (), 10);
  c.reset ();

  std::map<Database::IdT, unsigned> targetMap;
  for (unsigned i = 0; i < nTargets; ++i)
    {
      std::ostringstream name;
      name << "target " << i;

      c = characters.CreateNew ("domob", name.str (), Faction::GREEN);
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
             const unsigned dmg)
  {
    AddAttackWithRange (pb, range).set_max_damage (dmg);
  }

  /**
   * Finds combat targets and deals damage.
   */
  std::vector<proto::TargetId>
  FindTargetsAndDamage ()
  {
    FindCombatTargets (db, rnd);
    return DealCombatDamage (db, rnd);
  }

};

TEST_F (DealDamageTests, NoAttacks)
{
  auto c = characters.CreateNew ("domob", "attacker", Faction::RED);
  NoAttacks (c->MutableProto ());
  c.reset ();

  c = characters.CreateNew ("domob", "target", Faction::GREEN);
  const auto idTarget = c->GetId ();
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (10);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 10);
}

TEST_F (DealDamageTests, OnlyAttacksInRange)
{
  auto c = characters.CreateNew ("domob", "attacker", Faction::RED);
  AddAttack (c->MutableProto (), 1, 1);
  AddAttack (c->MutableProto (), 2, 1);
  AddAttack (c->MutableProto (), 3, 1);
  c.reset ();

  c = characters.CreateNew ("domob", "target", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (2, 0));
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (10);
  c.reset ();

  FindTargetsAndDamage ();
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 8);
}

TEST_F (DealDamageTests, RandomisedDamage)
{
  constexpr unsigned dmg = 10;
  constexpr unsigned rolls = 1000;
  constexpr unsigned threshold = rolls / dmg * 80 / 100;

  auto c = characters.CreateNew ("domob", "attacker", Faction::RED);
  AddAttack (c->MutableProto (), 1, dmg);
  c.reset ();

  c = characters.CreateNew ("domob", "target", Faction::GREEN);
  const auto idTarget = c->GetId ();
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_armour (dmg * rolls);
  c.reset ();

  std::vector<unsigned> cnts(dmg + 1);
  for (unsigned i = 0; i < rolls; ++i)
    {
      const int before = characters.GetById (idTarget)->GetHP ().armour ();
      FindTargetsAndDamage ();
      const int after = characters.GetById (idTarget)->GetHP ().armour ();

      const int dmgDone = before - after;
      ASSERT_GE (dmgDone, 1);
      ASSERT_LE (dmgDone, dmg);

      ++cnts[dmgDone];
    }

  for (unsigned i = 1; i <= dmg; ++i)
    {
      LOG (INFO) << "Damage " << i << " done: " << cnts[i] << " times";
      EXPECT_GE (cnts[i], threshold);
    }
}

TEST_F (DealDamageTests, HpReduction)
{
  constexpr unsigned trials = 100;

  auto c = characters.CreateNew ("domob", "attacker", Faction::RED);
  const auto idAttacker = c->GetId ();
  c.reset ();

  c = characters.CreateNew ("domob", "target", Faction::GREEN);
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
      AddAttack (c->MutableProto (), 1, t.dmg);
      c.reset ();

      /* Since the total damage dealt is random, we cannot enforce directly
         that all of t.dmg is dealt.  Thus we roll multiple times until we
         actually hit a situation where the expectation is satisfied.  */
      bool found = false;
      for (unsigned i = 0; i < trials; ++i)
        {
          c = characters.GetById (idTarget);
          c->MutableHP ().set_shield (t.hpBeforeShield);
          c->MutableHP ().set_armour (t.hpBeforeArmour);
          c.reset ();

          FindTargetsAndDamage ();

          c = characters.GetById (idTarget);
          EXPECT_GE (c->GetHP ().shield (), t.hpAfterShield);
          EXPECT_GE (c->GetHP ().armour (), t.hpAfterArmour);

          if (c->GetHP ().shield () == t.hpAfterShield
                && c->GetHP ().armour () == t.hpAfterArmour)
            {
              found = true;
              break;
            }
        }
      EXPECT_TRUE (found);
    }
}

TEST_F (DealDamageTests, Kills)
{
  auto c = characters.CreateNew ("domob", "attacker 1", Faction::RED);
  AddAttack (c->MutableProto (), 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", "attacker 2", Faction::RED);
  AddAttack (c->MutableProto (), 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", "attacker 3", Faction::RED);
  c->SetPosition (HexCoord (10, 10));
  AddAttack (c->MutableProto (), 1, 1);
  c.reset ();

  c = characters.CreateNew ("domob", "target 1", Faction::GREEN);
  const auto id1 = c->GetId ();
  NoAttacks (c->MutableProto ());
  c->MutableHP ().set_shield (0);
  c->MutableHP ().set_armour (1);
  c.reset ();

  c = characters.CreateNew ("domob", "target 2", Faction::GREEN);
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

} // anonymous namespace
} // namespace pxd
