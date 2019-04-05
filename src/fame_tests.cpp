#include "fame_tests.hpp"

#include "database/dbtest.hpp"
#include "proto/combat.pb.h"

#include <gtest/gtest.h>

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

class FameTests : public DBTestWithSchema
{

protected:

  CharacterTable characters;
  AccountsTable accounts;

  /** Updater instance used in testing.  */
  FameUpdater fame;

  FameTests ()
    : characters(db), accounts(db), fame(db, 0)
  {}

  /**
   * Calls the FameUpdater::UpdateForKill method for the given data.
   */
  void
  UpdateForKill (const Database::IdT victim,
                 const DamageLists::Attackers& attackers)
  {
    fame.UpdateForKill (victim, attackers);
  }

};

namespace
{

TEST_F (FameTests, TrackingKills)
{
  const auto id1 = characters.CreateNew ("foo", Faction::RED)->GetId ();
  const auto id2 = characters.CreateNew ("foo", Faction::RED)->GetId ();
  const auto id3 = characters.CreateNew ("bar", Faction::RED)->GetId ();
  const auto id4 = characters.CreateNew ("bar", Faction::RED)->GetId ();

  /* Add initial data to make sure that's taken into account.  */
  accounts.GetByName ("foo")->SetKills (10);

  /* Multiple killers (including the character owner himself) as well as
     multiple killing characters of one owner.  */
  UpdateForKill (id4, {id1, id2, id3});

  EXPECT_EQ (accounts.GetByName ("foo")->GetKills (), 11);
  EXPECT_EQ (accounts.GetByName ("bar")->GetKills (), 1);
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
