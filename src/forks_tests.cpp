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

#include "forks.hpp"

#include "testutils.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class ForksTests : public testing::Test
{

protected:

  ContextForTesting ctx;

  ForksTests ()
  {}

};

TEST_F (ForksTests, IsActive)
{
  ctx.SetChain (xaya::Chain::REGTEST);
  ctx.SetBlockHeight (99);
  EXPECT_FALSE (ctx.Forks ().IsActive (Fork::Dummy));
  ctx.SetBlockHeight (100);
  EXPECT_TRUE (ctx.Forks ().IsActive (Fork::Dummy));
  ctx.SetBlockHeight (101);
  EXPECT_TRUE (ctx.Forks ().IsActive (Fork::Dummy));

  ctx.SetChain (xaya::Chain::MAIN);
  EXPECT_FALSE (ctx.Forks ().IsActive (Fork::Dummy));
  ctx.SetBlockHeight (3'000'000);
  EXPECT_TRUE (ctx.Forks ().IsActive (Fork::Dummy));
}

/* Forks are keyed to heights of the underlying chain, while Context::Height ()
   is the superblock counter and starts back at one for every game.  Activation
   must therefore follow the block height alone, no matter how far the two have
   drifted apart.  */
TEST_F (ForksTests, FollowsChainHeightNotSuperblockHeight)
{
  ctx.SetChain (xaya::Chain::REGTEST);

  ctx.SetHeight (1'000'000);
  ctx.SetBlockHeight (99);
  EXPECT_FALSE (ctx.Forks ().IsActive (Fork::Dummy));

  ctx.SetHeight (1);
  ctx.SetBlockHeight (100);
  EXPECT_TRUE (ctx.Forks ().IsActive (Fork::Dummy));
}

} // anonymous namespace
} // namespace pxd
