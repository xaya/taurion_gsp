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

#include "skills.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace pxd
{

/**
 * Subclass of SkillManager for testing, which gives us access
 * e.g. to even constructing an instance directly.
 */
class TestSkillManager : public SkillManager
{

public:

  explicit TestSkillManager (Database& d, const std::string& n)
    : SkillManager(d, n)
  {}

};

namespace
{

struct CountResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, cnt, 1);
};

class SkillManagerTests : public DBTestWithSchema
{

protected:

  /**
   * Queries the database for the number of explicit XP entries
   * for the given account.  We use this to ensure that only those
   * entries are created that should be (e.g. no entries corresponding
   * to unmodified zeros).
   */
  unsigned
  CountXpEntries (const std::string& account)
  {
    auto stmt = db.Prepare (R"(
      SELECT COUNT(*) AS `cnt`
        FROM `account_xps`
        WHERE `name` = ?1
    )");
    stmt.Bind (1, account);

    auto res = stmt.Query<CountResult> ();
    CHECK (res.Step ());
    const unsigned cnt = res.Get<CountResult::cnt> ();
    CHECK (!res.Step ());

    return cnt;
  }

};

TEST_F (SkillManagerTests, NoEntries)
{
  auto m = std::make_unique<TestSkillManager> (db, "foo");
  EXPECT_EQ ((*m)[proto::SKILL_BUILDING].GetXp (), 0);
  m.reset ();

  EXPECT_EQ (CountXpEntries ("foo"), 0);
}

TEST_F (SkillManagerTests, BasicXpUpdates)
{
  auto m = std::make_unique<TestSkillManager> (db, "foo");
  (*m)[proto::SKILL_BUILDING].AddXp (1);
  (*m)[proto::SKILL_COMBAT].AddXp (10);
  (*m)[proto::SKILL_BUILDING].AddXp (2);
  EXPECT_EQ ((*m)[proto::SKILL_BUILDING].GetXp (), 3);
  EXPECT_EQ ((*m)[proto::SKILL_COMBAT].GetXp (), 10);
  m.reset ();

  EXPECT_EQ (CountXpEntries ("foo"), 2);

  m = std::make_unique<TestSkillManager> (db, "foo");
  EXPECT_EQ ((*m)[proto::SKILL_BUILDING].GetXp (), 3);
  (*m)[proto::SKILL_BUILDING].AddXp (2);
  EXPECT_EQ ((*m)[proto::SKILL_BUILDING].GetXp (), 5);
  m.reset ();

  EXPECT_EQ (CountXpEntries ("foo"), 2);
}

TEST_F (SkillManagerTests, MultipleAccounts)
{
  auto m1 = std::make_unique<TestSkillManager> (db, "foo");
  (*m1)[proto::SKILL_BUILDING].AddXp (42);
  m1.reset ();

  EXPECT_EQ (CountXpEntries ("foo"), 1);
  EXPECT_EQ (CountXpEntries ("bar"), 0);

  m1 = std::make_unique<TestSkillManager> (db, "foo");
  auto m2 = std::make_unique<TestSkillManager> (db, "bar");
  (*m1)[proto::SKILL_BUILDING].AddXp (10);
  (*m2)[proto::SKILL_COMBAT].AddXp (100);
  m2.reset ();

  m2 = std::make_unique<TestSkillManager> (db, "bar");
  EXPECT_EQ ((*m1)[proto::SKILL_BUILDING].GetXp (), 52);
  EXPECT_EQ ((*m1)[proto::SKILL_COMBAT].GetXp (), 0);
  EXPECT_EQ ((*m2)[proto::SKILL_BUILDING].GetXp (), 0);
  EXPECT_EQ ((*m2)[proto::SKILL_COMBAT].GetXp (), 100);
  m1.reset ();
  m2.reset ();

  EXPECT_EQ (CountXpEntries ("foo"), 1);
  EXPECT_EQ (CountXpEntries ("bar"), 1);
}

TEST_F (SkillManagerTests, InvalidXpAdds)
{
  auto m = std::make_unique<TestSkillManager> (db, "foo");
  (*m)[proto::SKILL_BUILDING].AddXp (std::numeric_limits<int64_t>::max () - 10);
  m.reset ();

  m = std::make_unique<TestSkillManager> (db, "foo");
  EXPECT_DEATH ((*m)[proto::SKILL_COMBAT].AddXp (0), "is not positive");
  EXPECT_DEATH ((*m)[proto::SKILL_COMBAT].AddXp (-5), "is not positive");
  (*m)[proto::SKILL_COMBAT].AddXp (std::numeric_limits<int64_t>::max () - 10);
  EXPECT_DEATH ((*m)[proto::SKILL_BUILDING].AddXp (11), "overflow");
  EXPECT_DEATH ((*m)[proto::SKILL_COMBAT].AddXp (11), "overflow");
  EXPECT_DEATH (
      (*m)[proto::SKILL_COMBAT].AddXp (std::numeric_limits<int64_t>::max ()),
      "overflow");
  m.reset ();
}

} // anonymous namespace
} // namespace pxd
