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

namespace
{

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

} // anonymous namespace
} // namespace pxd
