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

#include "moneysupply.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class MoneySupplyTests : public DBTestWithSchema
{

protected:

  MoneySupply m;

  MoneySupplyTests ()
    : m(db)
  {
    // DBTestWithSchema already calls InitialiseDatabase.
  }

};

TEST_F (MoneySupplyTests, GetAndIncrement)
{
  EXPECT_EQ (m.Get ("burnsale"), 0);
  EXPECT_EQ (m.Get ("gifted"), 0);

  m.Increment ("burnsale", 42);
  m.Increment ("burnsale", 100);
  m.Increment ("gifted", 1);

  EXPECT_EQ (m.Get ("burnsale"), 142);
  EXPECT_EQ (m.Get ("gifted"), 1);
}

TEST_F (MoneySupplyTests, AllKeys)
{
  for (const auto& s : m.GetValidKeys ())
    m.Get (s);
}

TEST_F (MoneySupplyTests, DoubleInitialisation)
{
  EXPECT_DEATH (m.InitialiseDatabase (), "UNIQUE constraint failed");
}

TEST_F (MoneySupplyTests, InvalidCalls)
{
  EXPECT_DEATH (m.Get ("invalid"), "Invalid key: invalid");
  EXPECT_DEATH (m.Increment ("invalid", 1), "Invalid key: invalid");
  EXPECT_DEATH (m.Increment ("burnsale", 0), "value > 0");
  EXPECT_DEATH (m.Increment ("burnsale", -10), "value > 0");
}

} // anonymous namespace
} // namespace pxd
