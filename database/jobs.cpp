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

#include <glog/logging.h>

namespace pxd
{

Job::Job (Database& d)
  : db(d), id(db.GetNextId ()), tracker(db.TrackHandle ("job", id)),
    reward(0), collateral(0), deadline(0),
    dirtyFields(true)
{
  VLOG (1) << "Created new job with ID " << id;
  data.SetToDefault ();
}

Job::Job (Database& d, const Database::Result<JobResult>& res)
  : db(d), dirtyFields(false)
{
  id = res.Get<JobResult::id> ();
  tracker = db.TrackHandle ("job", id);

  poster = res.Get<JobResult::poster> ();

  if (res.IsNull<JobResult::worker> ())
    worker = "";
  else
    worker = res.Get<JobResult::worker> ();

  reward = res.Get<JobResult::reward> ();
  collateral = res.Get<JobResult::collateral> ();
  deadline = res.Get<JobResult::deadline> ();

  data = res.GetProto<JobResult::proto> ();

  VLOG (1) << "Created job instance for ID " << id << " from database";
}

Job::~Job ()
{
  if (!dirtyFields && !data.IsDirty ())
    {
      VLOG (1) << "Job " << id << " is not dirty";
      return;
    }

  VLOG (1) << "Updating dirty job " << id << " in the database";

  CHECK (!poster.empty ()) << "Job " << id << " has no poster set";
  CHECK_GE (reward, 0) << "Job " << id << " has negative reward";
  CHECK_GE (collateral, 0) << "Job " << id << " has negative collateral";

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `jobs`
      (`id`, `poster`, `worker`,
       `reward`, `collateral`, `deadline`, `proto`)
      VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
  )");

  stmt.Bind (1, id);
  stmt.Bind (2, poster);

  if (worker.empty ())
    stmt.BindNull (3);
  else
    stmt.Bind (3, worker);

  stmt.Bind (4, reward);
  stmt.Bind (5, collateral);
  stmt.Bind (6, deadline);
  stmt.BindProto (7, data);

  stmt.Execute ();
}

JobsTable::Handle
JobsTable::CreateNew (const std::string& poster, const Amount reward,
                      const Amount collateral, const int64_t deadline)
{
  Handle j(new Job (db));

  j->poster = poster;
  j->reward = reward;
  j->collateral = collateral;
  j->deadline = deadline;

  return j;
}

JobsTable::Handle
JobsTable::GetFromResult (const Database::Result<JobResult>& res)
{
  return Handle (new Job (db, res));
}

JobsTable::Handle
JobsTable::GetById (const Database::IdT id)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `jobs`
      WHERE `id` = ?1
  )");
  stmt.Bind (1, id);
  auto res = stmt.Query<JobResult> ();
  if (!res.Step ())
    return nullptr;

  auto j = GetFromResult (res);
  CHECK (!res.Step ());
  return j;
}

Database::Result<JobResult>
JobsTable::QueryAll ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `jobs`
      ORDER BY `id`
  )");
  return stmt.Query<JobResult> ();
}

Database::Result<JobResult>
JobsTable::QueryForDeadline (const int64_t now)
{
  /* This order is the one of the index on the deadline (which has the ID
     as rowid), so that nothing needs to be sorted.  */
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `jobs`
      WHERE `deadline` <= ?1
      ORDER BY `deadline`, `id`
  )");
  stmt.Bind (1, now);
  return stmt.Query<JobResult> ();
}

namespace
{

/** Result type for the COUNT queries.  */
struct CountResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, cnt, 1);
};

/** Runs a COUNT query and returns its value.  */
int64_t
StepCount (Database::Statement&& stmt)
{
  auto res = stmt.Query<CountResult> ();
  CHECK (res.Step ());
  const int64_t cnt = res.Get<CountResult::cnt> ();
  CHECK (!res.Step ());
  return cnt;
}

} // anonymous namespace

int64_t
JobsTable::CountAll () const
{
  return StepCount (db.Prepare (R"(
    SELECT COUNT(*) AS `cnt` FROM `jobs`
  )"));
}

int64_t
JobsTable::CountForPoster (const std::string& poster) const
{
  auto stmt = db.Prepare (R"(
    SELECT COUNT(*) AS `cnt`
      FROM `jobs`
      WHERE `poster` = ?1
  )");
  stmt.Bind (1, poster);
  return StepCount (std::move (stmt));
}

void
JobsTable::DeleteById (const Database::IdT id)
{
  auto stmt = db.Prepare (R"(
    DELETE FROM `jobs`
      WHERE `id` = ?1
  )");
  stmt.Bind (1, id);
  stmt.Execute ();
}

namespace
{

/** Result type for the queries of reserved coins.  */
struct ReservedResult : public Database::ResultType
{
  RESULT_COLUMN (std::string, account, 1);
  RESULT_COLUMN (int64_t, amount, 2);
};

} // anonymous namespace

std::map<std::string, Amount>
JobsTable::GetReservedCoins () const
{
  std::map<std::string, Amount> balances;

  {
    auto stmt = db.Prepare (R"(
      SELECT `poster` AS `account`, SUM(`reward`) AS `amount`
        FROM `jobs`
        GROUP BY `poster`
    )");
    auto res = stmt.Query<ReservedResult> ();
    while (res.Step ())
      balances[res.Get<ReservedResult::account> ()]
          += res.Get<ReservedResult::amount> ();
  }

  {
    auto stmt = db.Prepare (R"(
      SELECT `worker` AS `account`, SUM(`collateral`) AS `amount`
        FROM `jobs`
        WHERE `worker` IS NOT NULL
        GROUP BY `worker`
    )");
    auto res = stmt.Query<ReservedResult> ();
    while (res.Step ())
      balances[res.Get<ReservedResult::account> ()]
          += res.Get<ReservedResult::amount> ();
  }

  return balances;
}

} // namespace pxd
