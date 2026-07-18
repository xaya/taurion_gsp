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

#include "jobs.hpp"

#include "dbtest.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

namespace pxd
{
namespace
{

using testing::ElementsAre;
using testing::Pair;

class JobsTableTests : public DBTestWithSchema
{

protected:

  JobsTable tbl;

  JobsTableTests ()
    : tbl(db)
  {}

  /**
   * Creates an OPEN transport job with a deadline and linked destination
   * building, and returns the handle so the caller can fill in more.
   */
  JobsTable::Handle
  CreateTransport (const Faction f, const std::string& poster,
                   const Amount reward, const Amount collateral,
                   const int64_t deadline, const Database::IdT dest)
  {
    auto j = tbl.CreateNew (Job::Type::TRANSPORT, f, poster, reward,
                            collateral);
    j->SetDeadline (deadline);
    j->SetLinkedId (dest);
    return j;
  }

};

TEST_F (JobsTableTests, RoundTrip)
{
  Database::IdT id;
  {
    auto j = CreateTransport (Faction::RED, "poster", 2000, 8000, 1234, 42);
    (*j->MutableProto ().mutable_transport ()->mutable_manifest ()
         ->mutable_fungible ())["foo"] = 5;
    id = j->GetId ();
  }

  auto j = tbl.GetById (id);
  ASSERT_NE (j, nullptr);
  EXPECT_EQ (j->GetType (), Job::Type::TRANSPORT);
  EXPECT_EQ (j->GetStatus (), Job::Status::OPEN);
  EXPECT_EQ (j->GetFaction (), Faction::RED);
  EXPECT_EQ (j->GetPoster (), "poster");
  EXPECT_EQ (j->GetWorker (), "");
  EXPECT_EQ (j->GetReward (), 2000);
  EXPECT_EQ (j->GetCollateral (), 8000);
  ASSERT_TRUE (j->HasDeadline ());
  EXPECT_EQ (j->GetDeadline (), 1234);
  EXPECT_EQ (j->GetLinkedId (), 42);
  EXPECT_EQ (j->GetLinkedName (), "");
  EXPECT_EQ (j->GetProto ().transport ().manifest ().fungible ().at ("foo"), 5);
}

TEST_F (JobsTableTests, WorkerNullThenSet)
{
  Database::IdT id;
  {
    auto j = CreateTransport (Faction::RED, "poster", 100, 50, 10, 1);
    id = j->GetId ();
  }
  /* OPEN: worker column is NULL -> reads back empty.  */
  EXPECT_EQ (tbl.GetById (id)->GetWorker (), "");

  {
    auto j = tbl.GetById (id);
    j->SetWorker ("courier");
    j->SetStatus (Job::Status::ACCEPTED);
  }
  auto j = tbl.GetById (id);
  EXPECT_EQ (j->GetWorker (), "courier");
  EXPECT_EQ (j->GetStatus (), Job::Status::ACCEPTED);
}

TEST_F (JobsTableTests, StandingHasNoDeadline)
{
  /* A standing job (no deadline) must read back as HasDeadline() == false --
     never a coerced 0, which would expire every standing bounty.  */
  Database::IdT id;
  {
    auto j = tbl.CreateNew (Job::Type::TRANSPORT, Faction::RED, "poster",
                            100, 0);
    j->SetLinkedName ("target");
    id = j->GetId ();
  }
  auto j = tbl.GetById (id);
  EXPECT_FALSE (j->HasDeadline ());
  EXPECT_EQ (j->GetLinkedName (), "target");
  EXPECT_EQ (j->GetLinkedId (), Database::EMPTY_ID);
}

TEST_F (JobsTableTests, QueryForDeadlineExcludesStanding)
{
  db.SetNextId (101);
  CreateTransport (Faction::RED, "poster", 1, 0, 50, 1).reset ();
  CreateTransport (Faction::RED, "poster", 1, 0, 100, 1).reset ();
  CreateTransport (Faction::RED, "poster", 1, 0, 150, 1).reset ();
  /* A standing job with no deadline -- must never appear in the sweep.  */
  {
    auto s = tbl.CreateNew (Job::Type::TRANSPORT, Faction::RED, "poster", 1, 0);
    s->SetLinkedName ("target");
  }

  std::vector<Database::IdT> got;
  auto res = tbl.QueryForDeadline (100);
  while (res.Step ())
    got.push_back (tbl.GetFromResult (res)->GetId ());
  EXPECT_THAT (got, ElementsAre (101, 102));
}

TEST_F (JobsTableTests, QueryForDeadlineOrdersByDeadlineThenId)
{
  /* The sweep order is consensus: earliest deadline first, id breaking
     ties (which is also the jobs_by_deadline index order, so no sort).  */
  db.SetNextId (101);
  CreateTransport (Faction::RED, "poster", 1, 0, 100, 1).reset ();
  CreateTransport (Faction::RED, "poster", 1, 0, 50, 1).reset ();
  CreateTransport (Faction::RED, "poster", 1, 0, 50, 1).reset ();

  std::vector<Database::IdT> got;
  auto res = tbl.QueryForDeadline (100);
  while (res.Step ())
    got.push_back (tbl.GetFromResult (res)->GetId ());
  EXPECT_THAT (got, ElementsAre (102, 103, 101));
}

TEST_F (JobsTableTests, QueryForLinkedId)
{
  db.SetNextId (201);
  CreateTransport (Faction::RED, "poster", 1, 0, 10, 42).reset ();
  CreateTransport (Faction::RED, "poster", 1, 0, 10, 43).reset ();
  CreateTransport (Faction::RED, "poster", 1, 0, 10, 42).reset ();

  std::vector<Database::IdT> got;
  auto res = tbl.QueryForLinkedId (42);
  while (res.Step ())
    got.push_back (tbl.GetFromResult (res)->GetId ());
  EXPECT_THAT (got, ElementsAre (201, 203));
}

TEST_F (JobsTableTests, QueryForLinkedName)
{
  db.SetNextId (301);
  /* Two bounties on the same name (stacked pools), one on another, and a
     transport row whose NULL linked_name must never match.  */
  tbl.CreateNew (Job::Type::WANTED, Faction::INVALID, "poster", 100, 0)
      ->SetLinkedName ("badguy");
  tbl.CreateNew (Job::Type::WANTED, Faction::INVALID, "poster", 100, 0)
      ->SetLinkedName ("other");
  tbl.CreateNew (Job::Type::WANTED, Faction::INVALID, "poster", 100, 0)
      ->SetLinkedName ("badguy");
  CreateTransport (Faction::RED, "poster", 1, 0, 10, 1).reset ();

  std::vector<Database::IdT> got;
  auto res = tbl.QueryForLinkedName ("badguy");
  while (res.Step ())
    got.push_back (tbl.GetFromResult (res)->GetId ());
  EXPECT_THAT (got, ElementsAre (301, 303));

  EXPECT_TRUE (tbl.HasActiveBountyNames ());
}

TEST_F (JobsTableTests, HasActiveBountyNames)
{
  /* A NULL linked_name (transport) must not count as an active bounty.  */
  EXPECT_FALSE (tbl.HasActiveBountyNames ());
  CreateTransport (Faction::RED, "poster", 1, 0, 10, 1).reset ();
  EXPECT_FALSE (tbl.HasActiveBountyNames ());

  Database::IdT id;
  {
    auto j = tbl.CreateNew (Job::Type::WANTED, Faction::INVALID, "poster",
                            100, 0);
    j->SetLinkedName ("badguy");
    id = j->GetId ();
  }
  EXPECT_TRUE (tbl.HasActiveBountyNames ());

  tbl.DeleteById (id);
  EXPECT_FALSE (tbl.HasActiveBountyNames ());
}

TEST_F (JobsTableTests, AdmissionCapCounts)
{
  EXPECT_EQ (tbl.CountAll (), 0);

  CreateTransport (Faction::RED, "alice", 1, 0, 10, 42).reset ();
  CreateTransport (Faction::RED, "alice", 1, 0, 10, 42).reset ();
  CreateTransport (Faction::RED, "bob", 1, 0, 10, 7).reset ();
  tbl.CreateNew (Job::Type::WANTED, Faction::INVALID, "alice", 100, 0)
      ->SetLinkedName ("badguy");

  EXPECT_EQ (tbl.CountAll (), 4);
  EXPECT_EQ (tbl.CountForPoster ("alice"), 3);
  EXPECT_EQ (tbl.CountForPoster ("bob"), 1);
  EXPECT_EQ (tbl.CountForPoster ("nobody"), 0);
  EXPECT_EQ (tbl.CountForLinkedId (42), 2);
  EXPECT_EQ (tbl.CountForLinkedId (7), 1);
  EXPECT_EQ (tbl.CountForLinkedName ("badguy"), 1);
  EXPECT_EQ (tbl.CountForLinkedName ("other"), 0);
}

TEST_F (JobsTableTests, SetRewardDrainsPool)
{
  /* The wanted pool decrements the reward column as tranches pay out; the
     reserved-coins SUM must track the remaining escrow.  */
  Database::IdT id;
  {
    auto j = tbl.CreateNew (Job::Type::WANTED, Faction::INVALID, "poster",
                            9000, 0);
    j->SetLinkedName ("badguy");
    id = j->GetId ();
  }

  {
    auto j = tbl.GetById (id);
    j->SetReward (j->GetReward () - 3000);
  }

  EXPECT_EQ (tbl.GetById (id)->GetReward (), 6000);
  EXPECT_THAT (tbl.GetReservedCoins (), ElementsAre (Pair ("poster", 6000)));
}

TEST_F (JobsTableTests, ReservedCoins)
{
  /* poster: two posted rewards (2000 + 500); worker: one accepted collateral
     (8000).  The OPEN job's NULL worker must not be summed.  */
  CreateTransport (Faction::RED, "poster", 2000, 8000, 10, 1).reset ();
  {
    auto j = CreateTransport (Faction::RED, "poster", 500, 8000, 10, 1);
    j->SetWorker ("worker");
    j->SetStatus (Job::Status::ACCEPTED);
  }

  EXPECT_THAT (tbl.GetReservedCoins (),
               ElementsAre (Pair ("poster", 2500), Pair ("worker", 8000)));
}

/* ************************************************************************** */

using JobHistoryTests = JobsTableTests;

TEST_F (JobHistoryTests, RoundTrip)
{
  {
    auto j = CreateTransport (Faction::RED, "poster", 2000, 500, 1234, 42);
    j->SetWorker ("worker");
    j->SetStatus (Job::Status::ACCEPTED);
    j->MutableProto ().mutable_transport ();
    tbl.WriteHistory (*j, JobOutcome::COMPLETED, 100, 5000);
  }

  auto res = tbl.QueryHistory (0);
  ASSERT_TRUE (res.Step ());
  auto e = tbl.GetFromResult (res);
  EXPECT_FALSE (res.Step ());

  EXPECT_EQ (e->GetType (), Job::Type::TRANSPORT);
  EXPECT_EQ (e->GetOutcome (), JobOutcome::COMPLETED);
  EXPECT_EQ (e->GetSettledHeight (), 100);
  EXPECT_EQ (e->GetSettledTime (), 5000);
  EXPECT_EQ (e->GetFaction (), Faction::RED);
  EXPECT_EQ (e->GetPoster (), "poster");
  EXPECT_EQ (e->GetWorker (), "worker");
  EXPECT_EQ (e->GetReward (), 2000);
  EXPECT_EQ (e->GetCollateral (), 500);
  ASSERT_TRUE (e->HasDeadline ());
  EXPECT_EQ (e->GetDeadline (), 1234);
  EXPECT_EQ (e->GetLinkedId (), 42);
  EXPECT_TRUE (e->GetProto ().has_transport ());
}

TEST_F (JobHistoryTests, NullableColumns)
{
  {
    auto j = tbl.CreateNew (Job::Type::WANTED, Faction::INVALID, "poster",
                            0, 0);
    j->SetLinkedName ("target");
    tbl.WriteHistory (*j, JobOutcome::DRAINED, 7, 900);
  }

  auto res = tbl.QueryHistory (0);
  ASSERT_TRUE (res.Step ());
  auto e = tbl.GetFromResult (res);

  EXPECT_EQ (e->GetOutcome (), JobOutcome::DRAINED);
  EXPECT_EQ (e->GetFaction (), Faction::INVALID);
  EXPECT_EQ (e->GetWorker (), "");
  EXPECT_FALSE (e->HasDeadline ());
  EXPECT_EQ (e->GetLinkedId (), Database::EMPTY_ID);
  EXPECT_EQ (e->GetLinkedName (), "target");
}

TEST_F (JobHistoryTests, QueryFromTimeAndOrder)
{
  {
    auto j = CreateTransport (Faction::RED, "poster", 1, 0, 10, 1);
    tbl.WriteHistory (*j, JobOutcome::CANCELLED, 1, 300);
  }
  {
    auto j = CreateTransport (Faction::RED, "poster", 2, 0, 10, 1);
    tbl.WriteHistory (*j, JobOutcome::VOID, 2, 100);
  }
  {
    auto j = CreateTransport (Faction::RED, "poster", 3, 0, 10, 1);
    tbl.WriteHistory (*j, JobOutcome::FAILED, 3, 200);
  }

  /* Ordered by settled_time; fromtime is inclusive.  */
  std::vector<Amount> rewards;
  auto res = tbl.QueryHistory (200);
  while (res.Step ())
    rewards.push_back (tbl.GetFromResult (res)->GetReward ());
  EXPECT_THAT (rewards, ElementsAre (3, 1));
}

TEST_F (JobHistoryTests, Prune)
{
  {
    auto j = CreateTransport (Faction::RED, "poster", 1, 0, 10, 1);
    tbl.WriteHistory (*j, JobOutcome::COMPLETED, 1, 100);
  }
  {
    auto j = CreateTransport (Faction::RED, "poster", 2, 0, 10, 1);
    tbl.WriteHistory (*j, JobOutcome::COMPLETED, 2, 200);
  }

  /* Strictly-before cutoff: the row AT the cutoff stays (the batch is
     ample, so it does not bite here).  */
  tbl.PruneHistory (200, 100);

  auto res = tbl.QueryHistory (0);
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetReward (), 2);
  EXPECT_FALSE (res.Step ());
}

TEST_F (JobHistoryTests, PruneBatched)
{
  /* Expired rows delete oldest-first in (settled_time, id) order, at most
     `batch` per call; ties on settled_time break by id.  */
  const int64_t times[] = {100, 100, 150, 180, 500};
  for (int i = 0; i < 5; ++i)
    {
      auto j = CreateTransport (Faction::RED, "poster", i + 1, 0, 10, 1);
      tbl.WriteHistory (*j, JobOutcome::COMPLETED, i + 1, times[i]);
    }

  const auto remaining = [this] ()
    {
      std::vector<Amount> res;
      auto r = tbl.QueryHistory (0);
      while (r.Step ())
        res.push_back (tbl.GetFromResult (r)->GetReward ());
      return res;
    };

  tbl.PruneHistory (200, 2);
  EXPECT_THAT (remaining (), ElementsAre (3, 4, 5));

  tbl.PruneHistory (200, 2);
  EXPECT_THAT (remaining (), ElementsAre (5));

  /* The remainder is not yet expired: further batches are no-ops.  */
  tbl.PruneHistory (200, 2);
  EXPECT_THAT (remaining (), ElementsAre (5));
}

TEST_F (JobHistoryTests, CursorPaginationCoversEveryRowOnce)
{
  /* Six settlements, including rows that share a settled_time, to exercise
     the (settled_time, id) tie-break in the cursor.  */
  const int64_t times[] = {100, 100, 200, 200, 200, 300};
  for (int i = 0; i < 6; ++i)
    {
      auto j = CreateTransport (Faction::RED, "poster", i + 1, 0, 10, 1);
      tbl.WriteHistory (*j, JobOutcome::COMPLETED, i + 1, times[i]);
    }

  /* Page one row at a time via the (settled_time, id) cursor; every row must
     appear exactly once, in (settled_time, id) order.  */
  std::vector<Amount> paged;
  int64_t afterT = 0, afterId = 0;
  for (;;)
    {
      auto res = tbl.QueryHistory (0, afterT, afterId, 1);
      if (!res.Step ())
        break;
      auto e = tbl.GetFromResult (res);
      paged.push_back (e->GetReward ());
      afterT = e->GetSettledTime ();
      afterId = e->GetId ();
      EXPECT_FALSE (res.Step ());   // the page limit of one is honoured
    }
  /* Rewards were assigned in id order and ids ascend with settled_time here,
     so the (settled_time, id) order is rewards 1..6.  */
  EXPECT_THAT (paged, ElementsAre (1, 2, 3, 4, 5, 6));
}

TEST_F (JobsTableTests, QueryPagePagesById)
{
  db.SetNextId (101);
  for (int i = 0; i < 5; ++i)
    CreateTransport (Faction::RED, "poster", 1, 0, 50, 1).reset ();

  /* First page of three, then the cursor continues from the last ID and
     the final short page ends the walk.  */
  std::vector<Database::IdT> got;
  {
    auto res = tbl.QueryPage (0, 3);
    while (res.Step ())
      got.push_back (tbl.GetFromResult (res)->GetId ());
  }
  EXPECT_THAT (got, ElementsAre (101, 102, 103));
  got.clear ();
  {
    auto res = tbl.QueryPage (103, 3);
    while (res.Step ())
      got.push_back (tbl.GetFromResult (res)->GetId ());
  }
  EXPECT_THAT (got, ElementsAre (104, 105));

  /* limit <= 0 and an over-cap limit both fall back to the cap (>= 5 here),
     so every row comes back.  */
  for (const int lim : {0, -1, JobsTable::MAX_PAGE + 1})
    {
      int n = 0;
      auto res = tbl.QueryPage (0, lim);
      while (res.Step ())
        ++n;
      EXPECT_EQ (n, 5);
    }
}

TEST_F (JobHistoryTests, LimitClampsToTheHardCap)
{
  for (int i = 0; i < 3; ++i)
    {
      auto j = CreateTransport (Faction::RED, "poster", i + 1, 0, 10, 1);
      tbl.WriteHistory (*j, JobOutcome::COMPLETED, i + 1, 100 + i);
    }
  /* limit <= 0 and an over-cap limit both fall back to the cap (>= 3 here),
     so every row comes back.  */
  for (const int lim : {0, -1, JobsTable::MAX_PAGE + 1})
    {
      int n = 0;
      auto res = tbl.QueryHistory (0, 0, 0, lim);
      while (res.Step ())
        ++n;
      EXPECT_EQ (n, 3);
    }
}

} // anonymous namespace
} // namespace pxd
