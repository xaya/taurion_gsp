/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "fitments.hpp"

#include "testutils.hpp"

#include "database/dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class CheckVehicleFitmentsTests : public testing::Test
{

protected:

  ContextForTesting ctx;

};

TEST_F (CheckVehicleFitmentsTests, ComplexityLimit)
{
  EXPECT_FALSE (CheckVehicleFitments ("chariot", {"bow", "bow"}, ctx));
  EXPECT_TRUE (CheckVehicleFitments ("chariot", {"sword", "sword"}, ctx));
}

TEST_F (CheckVehicleFitmentsTests, Slots)
{
  EXPECT_FALSE (CheckVehicleFitments ("noslots", {"sword"}, ctx));
  EXPECT_FALSE (CheckVehicleFitments ("chariot",
                                      {"sword", "sword", "sword", "sword"},
                                      ctx));
  EXPECT_TRUE (CheckVehicleFitments ("chariot", {
      "sword", "sword", "sword",
      "mid fitment", "mid fitment",
      "low fitment",
  }, ctx));
}

TEST_F (CheckVehicleFitmentsTests, ComplexityMultiplier)
{
  EXPECT_FALSE (CheckVehicleFitments ("chariot", {"bow", "bow"}, ctx));
  EXPECT_TRUE (CheckVehicleFitments ("chariot",
                                     {"bow", "bow", "super multiplier"},
                                     ctx));
}

TEST_F (CheckVehicleFitmentsTests, VehicleSize)
{
  EXPECT_FALSE (CheckVehicleFitments ("basetank", {"only medium"}, ctx));
  EXPECT_TRUE (CheckVehicleFitments ("chariot", {"only medium"}, ctx));
}

TEST_F (CheckVehicleFitmentsTests, FactionRestrictions)
{
  EXPECT_TRUE (CheckVehicleFitments ("basetank", {"bow"}, ctx));
  EXPECT_TRUE (CheckVehicleFitments ("rv vla", {"bow"}, ctx));
  EXPECT_TRUE (CheckVehicleFitments ("basetank", {"red fitment"}, ctx));
  EXPECT_TRUE (CheckVehicleFitments ("rv vla", {"red fitment"}, ctx));
  EXPECT_FALSE (CheckVehicleFitments ("gv vla", {"red fitment"}, ctx));
}

/* ************************************************************************** */

class DeriveCharacterStatsTests : public DBTestWithSchema
{

private:

  CharacterTable characters;

protected:

  ContextForTesting ctx;

  DeriveCharacterStatsTests ()
    : characters(db)
  {}

  /**
   * Constructs a character with the given vehicle and the given list
   * of fitments on it.
   */
  CharacterTable::Handle
  Derive (const std::string& vehicle,
          const std::vector<std::string>& fitments)
  {
    auto c = characters.CreateNew ("domob", Faction::RED);
    UpdateStats (*c, vehicle, fitments);
    return c;
  }

  /**
   * Re-derives the stats of the given character.
   */
  void
  UpdateStats (Character& c, const std::string& vehicle,
               const std::vector<std::string>& fitments)
  {
    c.MutableProto ().set_vehicle (vehicle);
    c.MutableProto ().clear_fitments ();
    for (const auto& f : fitments)
      c.MutableProto ().add_fitments (f);

    DeriveCharacterStats (c, ctx);
  }

};

TEST_F (DeriveCharacterStatsTests, BaseVehicleStats)
{
  auto c = Derive ("chariot", {});
  const auto& pb = c->GetProto ();
  EXPECT_EQ (pb.cargo_space (), 1'000);
  EXPECT_EQ (pb.speed (), 1'000);
  EXPECT_EQ (pb.combat_data ().attacks_size (), 2);
  EXPECT_EQ (pb.mining ().rate ().max (), 100);
  EXPECT_EQ (c->GetRegenData ().max_hp ().armour (), 1'000);
  EXPECT_EQ (c->GetRegenData ().max_hp ().shield (), 1'00);
  EXPECT_EQ (c->GetRegenData ().regeneration_mhp ().armour (), 0);
  EXPECT_EQ (c->GetRegenData ().regeneration_mhp ().shield (), 10);
}

TEST_F (DeriveCharacterStatsTests, ProspectingRate)
{
  EXPECT_EQ (Derive ("chariot", {})->GetProto ().prospecting_blocks (), 10);
  EXPECT_FALSE (Derive ("basetank", {})->GetProto ().has_prospecting_blocks ());
}

TEST_F (DeriveCharacterStatsTests, HpAreReset)
{
  auto c = Derive ("chariot", {});
  c->MutableHP ().set_armour (42);
  DeriveCharacterStats (*c, ctx);

  EXPECT_EQ (c->GetHP ().armour (), 1'000);
  EXPECT_EQ (c->GetHP ().shield (), 100);
}

TEST_F (DeriveCharacterStatsTests, FitmentAttacks)
{
  auto c = Derive ("chariot", {"lf bomb"});
  const auto& attacks = c->GetProto ().combat_data ().attacks ();
  ASSERT_EQ (attacks.size (), 3);
  EXPECT_EQ (attacks[0].range (), 100);
  EXPECT_EQ (attacks[1].area (), 10);
  EXPECT_EQ (attacks[2].area (), 3);
}

TEST_F (DeriveCharacterStatsTests, FitmentLowHpBoosts)
{
  auto c = Derive ("chariot", {"lf lowhpboost", "lf lowhpboost"});
  const auto& boosts = c->GetProto ().combat_data ().low_hp_boosts ();
  ASSERT_EQ (boosts.size (), 2);
  for (const auto& b : boosts)
    {
      EXPECT_EQ (b.max_hp_percent (), 10);
      EXPECT_EQ (b.damage ().percent (), 20);
      EXPECT_EQ (b.range ().percent (), 20);
    }
}

TEST_F (DeriveCharacterStatsTests, FitmentSelfDestructs)
{
  auto c = Derive ("chariot",
    {
      "lf selfdestruct",
      "lf selfdestruct",
      "lf rangeext",
      "lf rangeext",
      "lf dmgext",
    });
  const auto& sd = c->GetProto ().combat_data ().self_destructs ();
  ASSERT_EQ (sd.size (), 2);
  for (const auto& s : sd)
    {
      EXPECT_EQ (s.area (), 6);
      EXPECT_EQ (s.damage ().min (), 31);
      EXPECT_EQ (s.damage ().max (), 52);
    }
}

TEST_F (DeriveCharacterStatsTests, CargoSpeed)
{
  auto c = Derive ("chariot", {"lf turbo"});
  EXPECT_EQ (c->GetProto ().speed (), 1'100);

  c = Derive ("chariot", {"lf expander"});
  EXPECT_EQ (c->GetProto ().cargo_space (), 1'100);
}

TEST_F (DeriveCharacterStatsTests, ProspectingMining)
{
  auto c = Derive ("chariot", {"lf scanner", "lf pick"});
  EXPECT_EQ (c->GetProto ().prospecting_blocks (), 8);
  EXPECT_EQ (c->GetProto ().mining ().rate ().min (), 12);
  EXPECT_EQ (c->GetProto ().mining ().rate ().max (), 120);

  c = Derive ("chariot", {"super scanner", "super scanner"});
  EXPECT_EQ (c->GetProto ().prospecting_blocks (), 1);

  c = Derive ("basetank", {"lf scanner", "lf pick"});
  EXPECT_FALSE (c->GetProto ().has_prospecting_blocks ());
  EXPECT_FALSE (c->GetProto ().has_mining ());
}

TEST_F (DeriveCharacterStatsTests, MaxHpRegen)
{
  auto c = Derive ("chariot", {"lf plating", "lf shield"});
  EXPECT_EQ (c->GetRegenData ().max_hp ().armour (), 1'100);
  EXPECT_EQ (c->GetRegenData ().max_hp ().shield (), 110);

  c = Derive ("chariot", {"lf replenisher"});
  EXPECT_EQ (c->GetRegenData ().regeneration_mhp ().shield (), 11);
  EXPECT_EQ (c->GetRegenData ().regeneration_mhp ().armour (), 0);

  c = Derive ("chariot", {"lf armourregen"});
  EXPECT_EQ (c->GetRegenData ().regeneration_mhp ().shield (), 10);
  EXPECT_EQ (c->GetRegenData ().regeneration_mhp ().armour (), 2'000);
}

TEST_F (DeriveCharacterStatsTests, RangeDamage)
{
  auto c = Derive ("chariot", {"lf rangeext", "lf dmgext", "lf dmgext"});

  const auto* a = &c->GetProto ().combat_data ().attacks (0);
  EXPECT_FALSE (a->has_area ());
  EXPECT_EQ (a->range (), 110);
  EXPECT_EQ (a->damage ().min (), 11);
  EXPECT_EQ (a->damage ().max (), 110);

  a = &c->GetProto ().combat_data ().attacks (1);
  EXPECT_FALSE (a->has_range ());
  EXPECT_EQ (a->area (), 11);
}

TEST_F (DeriveCharacterStatsTests, ReceivedDamageAndHitChance)
{
  auto c = Derive ("chariot", {});
  EXPECT_FALSE (c->GetProto ().combat_data ().has_received_damage_modifier ());
  EXPECT_FALSE (c->GetProto ().combat_data ().has_hit_chance_modifier ());

  c = Derive ("chariot", {"lf dmgred", "lf dmgred", "lf hitext"});
  const auto& cd = c->GetProto ().combat_data ();
  EXPECT_EQ (cd.received_damage_modifier ().percent (), -10);
  EXPECT_EQ (cd.hit_chance_modifier ().percent (), 10);
}

TEST_F (DeriveCharacterStatsTests, StackingButNotCompounding)
{
  auto c = Derive ("chariot", {"lf turbo", "lf turbo", "lf turbo"});
  EXPECT_EQ (c->GetProto ().speed (), 1'300);
}

TEST_F (DeriveCharacterStatsTests, FitmentAttacksAlsoBoosted)
{
  auto c = Derive ("chariot",
    {
      "lf bomb",
      "lf dmgext", "lf dmgext", "lf dmgext",
    });
  const auto& a = c->GetProto ().combat_data ().attacks (2);
  EXPECT_EQ (a.damage ().max (), 10);
}

TEST_F (DeriveCharacterStatsTests, MobileRefinery)
{
  auto c = Derive ("chariot", {"vhf refinery", "vhf refinery"});
  EXPECT_EQ (c->GetProto ().refining ().input ().percent (), 100);

  UpdateStats (*c, "chariot", {});
  EXPECT_FALSE (c->GetProto ().has_refining ());
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
