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

#include "combat.hpp"

#include "proto/combat.pb.h"

#include <gtest/gtest.h>

#include <google/protobuf/text_format.h>

using google::protobuf::TextFormat;

namespace pxd
{

class ComputeCanRegenTests : public testing::Test
{

protected:

  /**
   * Calls ComputeCanRegen based on given text protos.
   */
  static bool
  CanRegen (const std::string& hpStr, const std::string& regenStr)
  {
    proto::HP hp;
    CHECK (TextFormat::ParseFromString (hpStr, &hp));
    proto::RegenData regen;
    CHECK (TextFormat::ParseFromString (regenStr, &regen));

    return CombatEntity::ComputeCanRegen (hp, regen);
  }

};

namespace
{

TEST_F (ComputeCanRegenTests, CanRegenerate)
{
  EXPECT_TRUE (CanRegen (R"(
    armour: 10
    shield: 0
  )", R"(
    max_hp: { armour: 10 shield: 10 }
    regeneration_mhp: { shield: 100 }
  )"));

  EXPECT_TRUE (CanRegen (R"(
    armour: 0
    shield: 10
  )", R"(
    max_hp: { armour: 10 shield: 10 }
    regeneration_mhp: { armour: 100 }
  )"));
}

TEST_F (ComputeCanRegenTests, NoRegeneration)
{
  EXPECT_FALSE (CanRegen (R"(
    armour: 0
    shield: 0
  )", R"(
    max_hp: { armour: 10 shield: 10 }
  )"));
}

TEST_F (ComputeCanRegenTests, FullHp)
{
  EXPECT_FALSE (CanRegen (R"(
    armour: 1
    shield: 10
  )", R"(
    max_hp: { armour: 10 shield: 10 }
    regeneration_mhp: { shield: 100 }
  )"));
  EXPECT_FALSE (CanRegen (R"(
    armour: 1
    shield: 0
  )", R"(
    max_hp: { armour: 10 shield: 0 }
    regeneration_mhp: { shield: 100 }
  )"));
  EXPECT_FALSE (CanRegen (R"(
    armour: 10
    shield: 1
  )", R"(
    max_hp: { armour: 10 shield: 10 }
    regeneration_mhp: { armour: 100 }
  )"));
}

} // anonymous namespace

class FindAttackRangeTests : public testing::Test
{

protected:

  /**
   * Calls FindAttackRange based on the combat data given as text proto.
   */
  static HexCoord::IntT
  FindRange (const std::string str)
  {
    proto::CombatData pb;
    CHECK (TextFormat::ParseFromString (str, &pb));

    return CombatEntity::FindAttackRange (pb);
  }

};

namespace
{

TEST_F (FindAttackRangeTests, NoAttacks)
{
  EXPECT_EQ (FindRange (""), CombatEntity::NO_ATTACKS);
}

TEST_F (FindAttackRangeTests, MaximumRange)
{
  EXPECT_EQ (FindRange (R"(
    attacks: { range: 5 }
    attacks: { range: 42 }
    attacks: { range: 1 }
  )"), 42);
}

TEST_F (FindAttackRangeTests, AreaAttacks)
{
  EXPECT_EQ (FindRange (R"(
    attacks: { range: 5 area: 2 }
  )"), 5);
  EXPECT_EQ (FindRange (R"(
    attacks: { area: 3 }
  )"), 3);
}

TEST_F (FindAttackRangeTests, ZeroRange)
{
  EXPECT_EQ (FindRange (R"(
    attacks: { range: 0 }
  )"), 0);
}

} // anonymous namespace
} // namespace pxd
