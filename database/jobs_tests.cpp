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

};

TEST_F (JobsTableTests, RoundTrip)
{
  Database::IdT id;
  {
    auto j = tbl.CreateNew ("poster", 2000, 8000, 1234);
    j->MutableProto ().set_type_tag (5);
    id = j->GetId ();
  }

  auto j = tbl.GetById (id);
  ASSERT_NE (j, nullptr);
  EXPECT_EQ (j->GetStatus (), Job::Status::OPEN);
  EXPECT_EQ (j->GetPoster (), "poster");
  EXPECT_EQ (j->GetWorker (), "");
  EXPECT_EQ (j->GetReward (), 2000);
  EXPECT_EQ (j->GetCollateral (), 8000);
  EXPECT_EQ (j->GetDeadline (), 1234);
  EXPECT_EQ (j->GetProto ().type_tag (), 5);
}

TEST_F (JobsTableTests, WorkerNullThenSet)
{
  Database::IdT id;
  {
    auto j = tbl.CreateNew ("poster", 100, 50, 10);
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

TEST_F (JobsTableTests, QueryForDeadlineIsInclusiveOfNow)
{
  /* The sweep takes everything at or before now; a later deadline stays.  */
  db.SetNextId (101);
  tbl.CreateNew ("poster", 1, 0, 50).reset ();
  tbl.CreateNew ("poster", 1, 0, 100).reset ();
  tbl.CreateNew ("poster", 1, 0, 150).reset ();

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
  tbl.CreateNew ("poster", 1, 0, 100).reset ();
  tbl.CreateNew ("poster", 1, 0, 50).reset ();
  tbl.CreateNew ("poster", 1, 0, 50).reset ();

  std::vector<Database::IdT> got;
  auto res = tbl.QueryForDeadline (100);
  while (res.Step ())
    got.push_back (tbl.GetFromResult (res)->GetId ());
  EXPECT_THAT (got, ElementsAre (102, 103, 101));
}

TEST_F (JobsTableTests, AdmissionCapCounts)
{
  EXPECT_EQ (tbl.CountAll (), 0);

  tbl.CreateNew ("alice", 1, 0, 10).reset ();
  tbl.CreateNew ("alice", 1, 0, 10).reset ();
  tbl.CreateNew ("bob", 1, 0, 10).reset ();

  EXPECT_EQ (tbl.CountAll (), 3);
  EXPECT_EQ (tbl.CountForPoster ("alice"), 2);
  EXPECT_EQ (tbl.CountForPoster ("bob"), 1);
  EXPECT_EQ (tbl.CountForPoster ("nobody"), 0);
}

TEST_F (JobsTableTests, DeleteById)
{
  Database::IdT id;
  {
    auto j = tbl.CreateNew ("poster", 1, 0, 10);
    id = j->GetId ();
  }
  ASSERT_NE (tbl.GetById (id), nullptr);

  tbl.DeleteById (id);
  EXPECT_EQ (tbl.GetById (id), nullptr);
  EXPECT_EQ (tbl.CountAll (), 0);
}

TEST_F (JobsTableTests, ReservedCoins)
{
  /* poster: two posted rewards (2000 + 500); worker: one accepted collateral
     (8000).  The OPEN job's NULL worker must not be summed.  */
  tbl.CreateNew ("poster", 2000, 8000, 10).reset ();
  {
    auto j = tbl.CreateNew ("poster", 500, 8000, 10);
    j->SetWorker ("worker");
    j->SetStatus (Job::Status::ACCEPTED);
  }

  EXPECT_THAT (tbl.GetReservedCoins (),
               ElementsAre (Pair ("poster", 2500), Pair ("worker", 8000)));
}

} // anonymous namespace
} // namespace pxd
