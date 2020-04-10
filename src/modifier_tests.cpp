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

#include "modifier.hpp"

#include <glog/logging.h>
#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using google::protobuf::TextFormat;

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

} // anonymous namespace
} // namespace pxd
