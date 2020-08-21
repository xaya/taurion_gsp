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

#include "burnsale.hpp"

#include "testutils.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class BurnsaleTests : public testing::Test
{

private:

  ContextForTesting ctx;

protected:

  /**
   * Computes the burnsale amount for the given parameters and
   * verifies that it matches the expected amount of vCHI gotten
   * and CHI used up.
   */
  void
  CheckAmounts (const Amount burntChi, const Amount alreadySold,
                const Amount expectedCoins, const Amount expectedChiUsed)
  {
    Amount remainingChi = burntChi;
    ASSERT_EQ (ComputeBurnsaleAmount (remainingChi, alreadySold, ctx),
               expectedCoins);
    EXPECT_EQ (remainingChi + expectedChiUsed, burntChi);
  }

};

TEST_F (BurnsaleTests, WithinOneStage)
{
  CheckAmounts (2 * COIN, 0, 20'000, 2 * COIN);
  CheckAmounts (COIN, 15'000'000'000, 5'000, COIN);
  CheckAmounts (500 * COIN, 29'999'000'000, 1'000'000, 500 * COIN);
  CheckAmounts (COIN / 1'000, 30'000'000'000, 1, COIN / 1'000);
}

TEST_F (BurnsaleTests, AcrossStageBoundary)
{
  CheckAmounts (300 * COIN, 9'999'000'000, 2'000'000, 300 * COIN);
}

TEST_F (BurnsaleTests, Rounding)
{
  CheckAmounts (COIN + 9'999, 0, 10'000, COIN);
  CheckAmounts (COIN / 1'000 - 1, 30'000'000'000, 0, 0);
}

TEST_F (BurnsaleTests, SoldOut)
{
  CheckAmounts (COIN, 50'000'000'000, 0, 0);
  CheckAmounts (COIN, 49'999'999'999, 1, COIN / 1'000);
}

TEST_F (BurnsaleTests, AllInOne)
{
  CheckAmounts (30'000'000 * COIN, 0, 50'000'000'000, 28'000'000 * COIN);
}

} // anonymous namespace
} // namespace pxd
