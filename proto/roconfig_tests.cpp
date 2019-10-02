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

#include "roconfig.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

TEST (RoConfigTests, Parses)
{
  RoConfigData ();
}

TEST (RoConfigTests, IsSingleton)
{
  const auto* ptr1 = &RoConfigData ();
  const auto* ptr2 = &RoConfigData ();
  EXPECT_EQ (ptr1, ptr2);
}

} // anonymous namespace
} // namespace pxd
