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

#include "damagelists.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class DamageListsTests : public DBTestWithSchema
{

protected:

  /**
   * Expects that the attackers for the given victim ID match a golden set.
   */
  void
  ExpectAttackers (const Database::IdT victim,
                   const DamageLists::Attackers& expected)
  {
    DamageLists dl(db);
    ASSERT_EQ (dl.GetAttackers (victim), expected);
  }

  /**
   * Adds an entry with a given height.
   */
  void
  AddWithHeight (const Database::IdT victim, const Database::IdT attacker,
                 const unsigned height)
  {
    DamageLists dl(db, height);
    dl.AddEntry (victim, attacker);
  }

};

TEST_F (DamageListsTests, Basic)
{
  DamageLists dl(db, 100);
  dl.AddEntry (1, 2);
  dl.AddEntry (1, 3);
  dl.AddEntry (1, 2);
  dl.AddEntry (2, 1);

  ExpectAttackers (1, {2, 3});
  ExpectAttackers (2, {1});
  ExpectAttackers (42, {});
}

TEST_F (DamageListsTests, RemoveCharacter)
{
  DamageLists dl(db, 100);
  dl.AddEntry (1, 2);
  dl.AddEntry (1, 3);
  dl.AddEntry (2, 1);
  dl.AddEntry (3, 2);
  dl.RemoveCharacter (2);

  ExpectAttackers (1, {3});
  ExpectAttackers (2, {});
  ExpectAttackers (3, {});
}

TEST_F (DamageListsTests, RemoveOld)
{
  AddWithHeight (1, 2, 5);
  AddWithHeight (1, 3, 6);
  AddWithHeight (1, 4, 10);

  DamageLists dl(db, 10);
  dl.RemoveOld (5);

  ExpectAttackers (1, {3, 4});
}

TEST_F (DamageListsTests, RefreshHeight)
{
  AddWithHeight (1, 2, 1);
  AddWithHeight (1, 3, 1);
  AddWithHeight (1, 3, 2);

  DamageLists dl(db, 2);
  dl.RemoveOld (1);

  ExpectAttackers (1, {3});
}

TEST_F (DamageListsTests, RemoveOldLargeN)
{
  AddWithHeight (1, 2, 5);
  AddWithHeight (1, 3, 10);

  DamageLists dl(db, 100);
  dl.RemoveOld (500);

  ExpectAttackers (1, {2, 3});
}

TEST_F (DamageListsTests, WithoutHeight)
{
  DamageLists dl(db);
  EXPECT_DEATH (dl.RemoveOld (5), "height != NO_HEIGHT");
  EXPECT_DEATH (dl.AddEntry (1, 2), "height != NO_HEIGHT");
}

} // anonymous namespace
} // namespace pxd
