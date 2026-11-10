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
#include <vector>

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
  EXPECT_FALSE (j->IsAccepted ());
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
  /* Without a worker, the column is NULL and reads back as empty.  */
  EXPECT_EQ (tbl.GetById (id)->GetWorker (), "");

  {
    auto j = tbl.GetById (id);
    j->SetWorker ("worker");
  }
  auto j = tbl.GetById (id);
  EXPECT_EQ (j->GetWorker (), "worker");
  EXPECT_TRUE (j->IsAccepted ());
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
  /* Earliest deadline first, and the ID for the same deadline (which is
     the order of the jobs_by_deadline index).  */
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
  /* The poster has both rewards, the worker the collateral of the job it
     accepted.  The other job has no worker for its collateral.  */
  tbl.CreateNew ("poster", 2000, 8000, 10).reset ();
  {
    auto j = tbl.CreateNew ("poster", 500, 8000, 10);
    j->SetWorker ("worker");
  }

  EXPECT_THAT (tbl.GetReservedCoins (),
               ElementsAre (Pair ("poster", 2500), Pair ("worker", 8000)));
}

} // anonymous namespace
} // namespace pxd
