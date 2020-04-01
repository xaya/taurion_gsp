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

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class FitmentsTests : public DBTestWithSchema
{

protected:

  CharacterTable characters;

  FitmentsTests ()
    : characters(db)
  {}

  /**
   * Returns a test character (with ID 1).
   */
  CharacterTable::Handle
  GetTest ()
  {
    auto c = characters.GetById (1);
    if (c != nullptr)
      return c;

    c = characters.CreateNew ("domob", Faction::RED);
    CHECK_EQ (c->GetId (), 1);
    return c;
  }

};

using DeriveCharacterStatsTests = FitmentsTests;

TEST_F (DeriveCharacterStatsTests, BaseVehicleStats)
{
  GetTest ()->MutableProto ().set_vehicle ("gv st");
  DeriveCharacterStats (*GetTest ());

  auto c = GetTest ();
  const auto& pb = c->GetProto ();
  EXPECT_EQ (pb.cargo_space (), 30);
  EXPECT_EQ (pb.speed (), 2'000);
  EXPECT_EQ (pb.combat_data ().attacks_size (), 1);
  EXPECT_EQ (pb.mining ().rate ().max (), 5);
  EXPECT_EQ (c->GetRegenData ().max_hp ().armour (), 150);
  EXPECT_EQ (c->GetRegenData ().max_hp ().shield (), 30);
  EXPECT_EQ (c->GetRegenData ().shield_regeneration_mhp (), 200);
}

TEST_F (DeriveCharacterStatsTests, HpAreReset)
{
  GetTest ()->MutableProto ().set_vehicle ("gv st");
  GetTest ()->MutableHP ().set_armour (42);
  DeriveCharacterStats (*GetTest ());

  EXPECT_EQ (GetTest ()->GetHP ().armour (), 150);
  EXPECT_EQ (GetTest ()->GetHP ().shield (), 30);
}

} // anonymous namespace
} // namespace pxd
