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
  EXPECT_FALSE (CheckVehicleFitments ("rv st", {"sword"}, ctx));
  EXPECT_FALSE (CheckVehicleFitments ("chariot",
                                      {"bomb", "bomb", "bomb", "bomb"},
                                      ctx));
  EXPECT_TRUE (CheckVehicleFitments ("chariot", {
      "bomb", "bomb", "bomb",
      "turbo", "turbo",
      "expander",
  }, ctx));
}

TEST_F (CheckVehicleFitmentsTests, ComplexityMultiplier)
{
  EXPECT_FALSE (CheckVehicleFitments ("chariot", {"bow", "turbo"}, ctx));
  EXPECT_TRUE (CheckVehicleFitments ("chariot",
                                     {"bow", "turbo", "multiplier"},
                                     ctx));
}

TEST_F (CheckVehicleFitmentsTests, VehicleSize)
{
  EXPECT_FALSE (CheckVehicleFitments ("basetank", {"only medium"}, ctx));
  EXPECT_TRUE (CheckVehicleFitments ("chariot", {"only medium"}, ctx));
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
    c->MutableProto ().set_vehicle (vehicle);
    for (const auto& f : fitments)
      c->MutableProto ().add_fitments (f);

    DeriveCharacterStats (*c, ctx);
    return c;
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
  EXPECT_EQ (c->GetRegenData ().shield_regeneration_mhp (), 10);
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
  auto c = Derive ("chariot", {"bomb"});
  const auto& attacks = c->GetProto ().combat_data ().attacks ();
  ASSERT_EQ (attacks.size (), 3);
  EXPECT_EQ (attacks[0].range (), 100);
  EXPECT_EQ (attacks[1].area (), 10);
  EXPECT_EQ (attacks[2].area (), 2);
}

TEST_F (DeriveCharacterStatsTests, FitmentLowHpBoosts)
{
  auto c = Derive ("chariot", {"lowhpboost", "lowhpboost"});
  const auto& boosts = c->GetProto ().combat_data ().low_hp_boosts ();
  ASSERT_EQ (boosts.size (), 2);
  for (const auto& b : boosts)
    {
      EXPECT_EQ (b.max_hp_percent (), 10);
      EXPECT_EQ (b.damage ().percent (), 50);
      EXPECT_EQ (b.range ().percent (), 20);
    }
}

TEST_F (DeriveCharacterStatsTests, FitmentSelfDestructs)
{
  auto c = Derive ("chariot",
    {
      "selfdestruct",
      "selfdestruct",
      "rangeext",
      "dmgext",
    });
  const auto& sd = c->GetProto ().combat_data ().self_destructs ();
  ASSERT_EQ (sd.size (), 2);
  for (const auto& s : sd)
    {
      EXPECT_EQ (s.area (), 11);
      EXPECT_EQ (s.damage ().min (), 11);
      EXPECT_EQ (s.damage ().max (), 33);
    }
}

TEST_F (DeriveCharacterStatsTests, CargoSpeed)
{
  auto c = Derive ("chariot", {"turbo"});
  EXPECT_EQ (c->GetProto ().speed (), 1'100);

  c = Derive ("chariot", {"expander"});
  EXPECT_EQ (c->GetProto ().cargo_space (), 1'100);
}

TEST_F (DeriveCharacterStatsTests, ProspectingMining)
{
  auto c = Derive ("chariot", {"scanner", "pick"});
  EXPECT_EQ (c->GetProto ().prospecting_blocks (), 8);
  EXPECT_EQ (c->GetProto ().mining ().rate ().min (), 12);
  EXPECT_EQ (c->GetProto ().mining ().rate ().max (), 120);

  c = Derive ("chariot", {"super scanner", "super scanner"});
  EXPECT_EQ (c->GetProto ().prospecting_blocks (), 1);

  c = Derive ("basetank", {"scanner", "pick"});
  EXPECT_FALSE (c->GetProto ().has_prospecting_blocks ());
  EXPECT_FALSE (c->GetProto ().has_mining ());
}

TEST_F (DeriveCharacterStatsTests, MaxHpRegen)
{
  auto c = Derive ("chariot", {"plating", "shield"});
  EXPECT_EQ (c->GetRegenData ().max_hp ().armour (), 1'100);
  EXPECT_EQ (c->GetRegenData ().max_hp ().shield (), 110);

  c = Derive ("chariot", {"replenisher"});
  EXPECT_EQ (c->GetRegenData ().shield_regeneration_mhp (), 11);
}

TEST_F (DeriveCharacterStatsTests, RangeDamage)
{
  auto c = Derive ("chariot", {"rangeext", "dmgext"});

  const auto* a = &c->GetProto ().combat_data ().attacks (0);
  EXPECT_FALSE (a->has_area ());
  EXPECT_EQ (a->range (), 110);
  EXPECT_EQ (a->damage ().min (), 11);
  EXPECT_EQ (a->damage ().max (), 110);

  a = &c->GetProto ().combat_data ().attacks (1);
  EXPECT_FALSE (a->has_range ());
  EXPECT_EQ (a->area (), 11);
}

TEST_F (DeriveCharacterStatsTests, StackingButNotCompounding)
{
  auto c = Derive ("chariot", {"turbo", "turbo", "turbo"});
  EXPECT_EQ (c->GetProto ().speed (), 1'300);
}

TEST_F (DeriveCharacterStatsTests, FitmentAttacksAlsoBoosted)
{
  auto c = Derive ("chariot", {"bomb", "dmgext", "dmgext"});
  const auto& a = c->GetProto ().combat_data ().attacks (2);
  EXPECT_EQ (a.damage ().max (), 6);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
