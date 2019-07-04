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

#include "fame_tests.hpp"

#include "database/dbtest.hpp"
#include "proto/combat.pb.h"

#include <gtest/gtest.h>

#include <sstream>

namespace pxd
{

using testing::_;

MockFameUpdater::MockFameUpdater (Database& db, const unsigned height)
  : FameUpdater(db, height)
{
  /* By default, expect no calls.  Tests should explicitly set the expectations
     themselves as needed.  */
  EXPECT_CALL (*this, UpdateForKill (_, _)).Times (0);
}

/* ************************************************************************** */

class FameLevelTests : public testing::Test
{

protected:

  static int
  GetFameLevel (const unsigned fame)
  {
    return FameUpdater::GetLevel (fame);
  }

};

namespace
{

TEST_F (FameLevelTests, Works)
{
  EXPECT_EQ (GetFameLevel (0), 0);
  EXPECT_EQ (GetFameLevel (999), 0);
  EXPECT_EQ (GetFameLevel (2000), 2);
  EXPECT_EQ (GetFameLevel (2500), 2);
  EXPECT_EQ (GetFameLevel (2999), 2);
  EXPECT_EQ (GetFameLevel (7999), 7);
  EXPECT_EQ (GetFameLevel (8000), 8);
  EXPECT_EQ (GetFameLevel (9999), 8);
}

TEST_F (FameLevelTests, Difference)
{
  EXPECT_EQ (GetFameLevel (5000) - GetFameLevel (2000), 3);
  EXPECT_EQ (GetFameLevel (2000) - GetFameLevel (5000), -3);
}

} // anonymous namespace

/* ************************************************************************** */

class FameTests : public DBTestWithSchema
{

private:

  /** Counter variable to create unique account names.  */
  unsigned cnt = 0;

  std::unique_ptr<FameUpdater> fame;

protected:

  CharacterTable characters;
  AccountsTable accounts;

  FameTests ()
    : characters(db), accounts(db)
  {}

  /**
   * Returns a new, unique name for tests.
   */
  std::string
  UniqueName ()
  {
    std::ostringstream out;
    out << "name " << ++cnt;
    return out.str ();
  }

  /**
   * Creates a character for the given name and returns its ID.
   */
  Database::IdT
  CreateCharacter (const std::string& owner)
  {
    return characters.CreateNew (owner, Faction::RED)->GetId ();
  }

  /**
   * Calls the FameUpdater::UpdateForKill method for the given data.
   */
  void
  UpdateForKill (const Database::IdT victim,
                 const DamageLists::Attackers& attackers)
  {
    if (fame == nullptr)
      fame = std::make_unique<FameUpdater> (db, 0);
    fame->UpdateForKill (victim, attackers);
  }

  /**
   * Flushes the fame delta cache so we can verify the updates.
   */
  void
  FlushDeltas ()
  {
    fame.reset ();
  }

};

namespace
{

TEST_F (FameTests, TrackingKills)
{
  const auto id1 = CreateCharacter ("foo");
  const auto id2 = CreateCharacter ("foo");
  const auto id3 = CreateCharacter ("bar");
  const auto id4 = CreateCharacter ("bar");

  /* Add initial data to make sure that's taken into account.  */
  accounts.GetByName ("foo")->SetKills (10);

  /* Multiple killers (including the character owner himself) as well as
     multiple killing characters of one owner.  */
  UpdateForKill (id4, {id1, id2, id3});

  EXPECT_EQ (accounts.GetByName ("foo")->GetKills (), 11);
  EXPECT_EQ (accounts.GetByName ("bar")->GetKills (), 1);
}

TEST_F (FameTests, BasicUpdates)
{
  struct Test
  {
    std::string name;
    unsigned oldVictimFame;
    std::vector<unsigned> oldKillerFames;
    unsigned newVictimFame;
    std::vector<unsigned> newKillerFames;
  };
  const Test tests[] = {
    {"basic", 500, {100}, 400, {200}},
    {"multiple", 500, {100, 200}, 400, {150, 250}},
    {"to zero", 80, {0, 100}, 0, {40, 140}},
    {"already zero", 0, {100}, 0, {100}},
    {"out of range", 500, {5000}, 500, {5000}},
    {"some out of range", 500, {100, 5000}, 400, {150, 5000}},
    {"max fame", 8000, {9950}, 7900, {9999}},
  };

  for (const auto& t : tests)
    {
      LOG (INFO) << "Test case " << t.name << "...";

      const std::string victimName = UniqueName ();
      const auto victimId = CreateCharacter (victimName);
      accounts.GetByName (victimName)->SetFame (t.oldVictimFame);

      std::vector<std::string> killerNames;
      DamageLists::Attackers killerIds;
      for (const auto f : t.oldKillerFames)
        {
          killerNames.push_back (UniqueName ());
          killerIds.insert (CreateCharacter (killerNames.back ()));
          accounts.GetByName (killerNames.back ())->SetFame (f);
        }

      UpdateForKill (victimId, killerIds);
      FlushDeltas ();

      EXPECT_EQ (accounts.GetByName (victimName)->GetFame (), t.newVictimFame);

      std::vector<unsigned> newKillerFames;
      for (const auto& nm : killerNames)
        newKillerFames.push_back (accounts.GetByName (nm)->GetFame ());
      EXPECT_EQ (newKillerFames, t.newKillerFames);
    }
}

TEST_F (FameTests, SelfKills)
{
  struct Test
  {
    unsigned oldFame;
    unsigned newFame;
  };
  constexpr Test tests[] =
    {
      {0, 0},
      {10, 10},
      {100, 100},
      {8000, 8000},
      {9899, 9899},
      {9950, 9950},
      {9999, 9999},
    };

  for (const auto& t : tests)
    {
      LOG (INFO) << "Testing with old fame " << t.oldFame << "...";

      const std::string name = UniqueName ();

      const auto id1 = CreateCharacter (name);
      const auto id2 = CreateCharacter (name);

      accounts.GetByName (name)->SetFame (t.oldFame);
      UpdateForKill (id1, {id2});
      FlushDeltas ();

      EXPECT_EQ (accounts.GetByName (name)->GetFame (), t.newFame);
    }
}

TEST_F (FameTests, AccountsWithMultipleCharacters)
{
  const auto id1 = CreateCharacter ("foo");
  const auto id2 = CreateCharacter ("foo");
  const auto id3 = CreateCharacter ("bar");
  const auto id4 = CreateCharacter ("bar");
  const auto id5 = CreateCharacter ("baz");

  accounts.GetByName ("baz")->SetFame (5000);
  UpdateForKill (id1, {id2, id3, id4, id5});
  FlushDeltas ();

  EXPECT_EQ (accounts.GetByName ("foo")->GetFame (), 33);
  EXPECT_EQ (accounts.GetByName ("bar")->GetFame (), 133);
  EXPECT_EQ (accounts.GetByName ("baz")->GetFame (), 5000);
}

TEST_F (FameTests, ZeroFloorForMultipleCharactersKilled)
{
  const auto id1 = CreateCharacter ("foo");
  const auto id2 = CreateCharacter ("foo");
  const auto id3 = CreateCharacter ("bar");
  const auto id4 = CreateCharacter ("baz");

  UpdateForKill (id1, {id3});
  UpdateForKill (id2, {id4});
  FlushDeltas ();

  EXPECT_EQ (accounts.GetByName ("foo")->GetFame (), 0);
  EXPECT_EQ (accounts.GetByName ("bar")->GetFame (), 200);
  EXPECT_EQ (accounts.GetByName ("baz")->GetFame (), 200);
}

TEST_F (FameTests, TemporarilyBeyondCap)
{
  /* If we go "temporarily" above the 9999 cap or below 0, we should still end
     up with the correct final difference applied and only capped then.  */
  const auto id1 = CreateCharacter ("a");
  const auto id2 = CreateCharacter ("b");
  const auto id3 = CreateCharacter ("c");
  const auto id4 = CreateCharacter ("d");

  accounts.GetByName ("a")->SetFame (9995);
  accounts.GetByName ("b")->SetFame (9500);
  accounts.GetByName ("c")->SetFame (10);
  accounts.GetByName ("d")->SetFame (100);

  /* id1 will gain 50 and lose 100 fame, going beyond the cap in between.  */
  UpdateForKill (id2, {id1, id3});
  UpdateForKill (id1, {id2});

  /* id3 will lose 20 and gain 100 fame, going below zero temporarily.  */
  UpdateForKill (id3, {id4});
  UpdateForKill (id3, {id4});
  UpdateForKill (id4, {id3});

  FlushDeltas ();
  EXPECT_EQ (accounts.GetByName ("a")->GetFame (), 9945);
  EXPECT_EQ (accounts.GetByName ("c")->GetFame (), 90);
}

TEST_F (FameTests, BasedOnOriginalFame)
{
  const auto id1 = CreateCharacter ("foo");
  const auto id2 = CreateCharacter ("bar");

  /* The foo account is set up to be just within range for bar.  But as soon
     as it gets more fame from a kill, it would be out of range.  Since all
     updates are based on the original fame level, though, it will be in
     range for all updates.  */
  accounts.GetByName ("foo")->SetFame (4999);
  accounts.GetByName ("bar")->SetFame (3000);

  UpdateForKill (id2, {id1});
  UpdateForKill (id2, {id1});
  FlushDeltas ();

  EXPECT_EQ (accounts.GetByName ("foo")->GetFame (), 5199);
  EXPECT_EQ (accounts.GetByName ("bar")->GetFame (), 2800);
}

/* ************************************************************************** */

using FameFrameworkTests = DBTestWithSchema;

TEST_F (FameFrameworkTests, UpdateForKill)
{
  MockFameUpdater fame(db, 0);

  fame.GetDamageLists ().AddEntry (1, 2);
  fame.GetDamageLists ().AddEntry (1, 3);

  EXPECT_CALL (fame, UpdateForKill (1, DamageLists::Attackers ({2, 3})));
  EXPECT_CALL (fame, UpdateForKill (2, DamageLists::Attackers ({})));

  proto::TargetId id;
  id.set_type (proto::TargetId::TYPE_BUILDING);
  id.set_id (42);
  fame.UpdateForKill (id);

  id.set_type (proto::TargetId::TYPE_CHARACTER);
  id.set_id (1);
  fame.UpdateForKill (id);
  id.set_id (2);
  fame.UpdateForKill (id);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
