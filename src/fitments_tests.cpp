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

#include "fitments.hpp"

#include "testutils.hpp"

#include "database/dbtest.hpp"

#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using google::protobuf::TextFormat;

/* ************************************************************************** */

class StatModifierTests : public testing::Test
{

protected:

  /**
   * Constructs a stat modifier from a text proto.
   */
  static StatModifier
  Modifier (const std::string& text)
  {
    proto::StatModifier m;
    CHECK (TextFormat::ParseFromString (text, &m));
    return m;
  }

};

TEST_F (StatModifierTests, Default)
{
  StatModifier m;
  m += proto::StatModifier ();

  EXPECT_EQ (m (0), 0);
  EXPECT_EQ (m (-5), -5);
  EXPECT_EQ (m (1'000), 1'000);
}

TEST_F (StatModifierTests, Application)
{
  StatModifier m = Modifier ("percent: 50");
  EXPECT_EQ (m (0), 0);
  EXPECT_EQ (m (-100), -150);
  EXPECT_EQ (m (1'000), 1'500);
  EXPECT_EQ (m (1), 1);
  EXPECT_EQ (m (3), 4);

  m = Modifier ("percent: -10");
  EXPECT_EQ (m (0), 0);
  EXPECT_EQ (m (-100), -90);
  EXPECT_EQ (m (10), 9);
}

TEST_F (StatModifierTests, Stacking)
{
  StatModifier m;
  m += Modifier ("percent: 100");
  m += Modifier ("percent: 100");
  m += Modifier ("percent: -100");
  m += Modifier ("percent: 100");

  EXPECT_EQ (m (100), 300);
}

/* ************************************************************************** */

class DeriveCharacterStatsTests : public DBTestWithSchema
{

private:

  CharacterTable characters;

protected:

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

    DeriveCharacterStats (*c);
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

TEST_F (DeriveCharacterStatsTests, HpAreReset)
{
  auto c = Derive ("chariot", {});
  c->MutableHP ().set_armour (42);
  DeriveCharacterStats (*c);

  EXPECT_EQ (c->GetHP ().armour (), 1'000);
  EXPECT_EQ (c->GetHP ().shield (), 100);
}

TEST_F (DeriveCharacterStatsTests, FitmentAttacks)
{
  auto c = Derive ("chariot", {"bomb"});
  const auto& attacks = c->GetProto ().combat_data ().attacks ();
  ASSERT_EQ (attacks.size (), 3);
  EXPECT_EQ (attacks[0].range (), 100);
  EXPECT_EQ (attacks[1].range (), 10);
  EXPECT_EQ (attacks[2].range (), 2);
}

TEST_F (DeriveCharacterStatsTests, CargoSpeed)
{
  auto c = Derive ("chariot", {"turbo"});
  EXPECT_EQ (c->GetProto ().speed (), 1'100);

  c = Derive ("chariot", {"expander"});
  EXPECT_EQ (c->GetProto ().cargo_space (), 1'100);
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
  const auto& a = c->GetProto ().combat_data ().attacks (0);
  EXPECT_EQ (a.range (), 110);
  EXPECT_EQ (a.min_damage (), 11);
  EXPECT_EQ (a.max_damage (), 110);
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
  EXPECT_EQ (a.max_damage (), 6);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
