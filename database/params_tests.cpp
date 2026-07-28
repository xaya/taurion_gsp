/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

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

#include "params.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class ParamsTableTests : public DBTestWithSchema
{

protected:

  ParamsTable p;

  ParamsTableTests ()
    : p(db)
  {}

};

TEST_F (ParamsTableTests, FallbackWithoutOverride)
{
  EXPECT_EQ (p.Get ("some-limit", 42), 42);
}

TEST_F (ParamsTableTests, SetOverridesAndReplaces)
{
  p.Set ("some-limit", 5);
  EXPECT_EQ (p.Get ("some-limit", 42), 5);

  p.Set ("some-limit", 7);
  EXPECT_EQ (p.Get ("some-limit", 42), 7);

  /* Other names are unaffected.  */
  EXPECT_EQ (p.Get ("other-limit", 100), 100);

  /* Zero and negative values are stored as-is (0 is the posting freeze).  */
  p.Set ("some-limit", 0);
  EXPECT_EQ (p.Get ("some-limit", 42), 0);
}

TEST_F (ParamsTableTests, RemoveResetsToFallback)
{
  p.Set ("some-limit", 5);
  p.Remove ("some-limit");
  EXPECT_EQ (p.Get ("some-limit", 42), 42);

  /* Removing an absent name is a no-op.  */
  p.Remove ("some-limit");
  EXPECT_EQ (p.Get ("some-limit", 42), 42);
}

} // anonymous namespace
} // namespace pxd
