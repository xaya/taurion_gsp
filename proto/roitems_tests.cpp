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

#include "roitems.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

TEST (RoItemsTests, BasicItem)
{
  EXPECT_EQ (RoItemData ("foo").space (), 10);

  EXPECT_NE (RoItemDataOrNull ("foo"), nullptr);
  EXPECT_EQ (RoItemDataOrNull ("invalid item"), nullptr);
}

TEST (RoItemsTests, Blueprints)
{
  EXPECT_EQ (RoItemDataOrNull ("bpo"), nullptr);
  EXPECT_EQ (RoItemDataOrNull ("bowbpo"), nullptr);
  EXPECT_EQ (RoItemDataOrNull ("bow bpo "), nullptr);

  EXPECT_EQ (RoItemDataOrNull ("foo bpo"), nullptr);
  EXPECT_EQ (RoItemDataOrNull ("foo bpc"), nullptr);

  const auto& orig = RoItemData ("bow bpo");
  ASSERT_TRUE (orig.has_is_blueprint ());
  EXPECT_EQ (orig.space (), 0);
  EXPECT_EQ (orig.is_blueprint ().for_item (), "bow");
  EXPECT_TRUE (orig.is_blueprint ().original ());

  const auto& copy = RoItemData ("bow bpc");
  ASSERT_TRUE (copy.has_is_blueprint ());
  EXPECT_EQ (copy.space (), 0);
  EXPECT_EQ (copy.is_blueprint ().for_item (), "bow");
  EXPECT_FALSE (copy.is_blueprint ().original ());
}

} // anonymous namespace
} // namespace pxd
