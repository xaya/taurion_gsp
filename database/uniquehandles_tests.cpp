/*
    GSP for the Taurion blockchain game
    Copyright (C) 2021  Autonomous Worlds Ltd

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

#include "uniquehandles.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using UniqueHandlesTests = testing::Test;

TEST_F (UniqueHandlesTests, AddRemove)
{
  UniqueHandles h;

  h.Add ("account", "foo");
  h.Add ("account", "bar");
  h.Add ("character", "foo");

  EXPECT_DEATH (h.Add ("character", "foo"), "is already active");
  EXPECT_DEATH (h.Remove ("character", "bar"), "is not active");

  h.Remove ("account", "bar");
  h.Add ("account", "bar");

  h.Remove ("account", "foo");
  h.Remove ("account", "bar");
  h.Remove ("character", "foo");
}

TEST_F (UniqueHandlesTests, DestructorCheck)
{
  auto h = std::make_unique<UniqueHandles> ();
  h->Add ("account", "foo");
  EXPECT_DEATH (h.reset (), "are still active");
  h->Remove ("account", "foo");
}

TEST_F (UniqueHandlesTests, Tracker)
{
  UniqueHandles h;

  UniqueHandles::Tracker a(h, "account", "foo");
  UniqueHandles::Tracker b(h, "character", 42);

  auto c = std::make_unique<UniqueHandles::Tracker> (h, "account", "bar");
  c.reset ();
  c = std::make_unique<UniqueHandles::Tracker> (h, "account", "bar");

  EXPECT_DEATH (new UniqueHandles::Tracker (h, "character", 42),
                "is already active");
}

} // anonymous namespace
} // namespace pxd
