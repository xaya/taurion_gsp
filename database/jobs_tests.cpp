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

} // anonymous namespace
} // namespace pxd
