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
    status(Status::OPEN), reward(0), collateral(0), deadline(0),
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

  status = static_cast<Status> (res.Get<JobResult::status> ());
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

  CHECK (status != Status::INVALID) << "Job " << id << " has no status set";
  CHECK (!poster.empty ()) << "Job " << id << " has no poster set";
  CHECK_GE (reward, 0) << "Job " << id << " has negative reward";
  CHECK_GE (collateral, 0) << "Job " << id << " has negative collateral";

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `jobs`
      (`id`, `status`, `poster`, `worker`,
       `reward`, `collateral`, `deadline`, `proto`)
      VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
  )");

  stmt.Bind (1, id);
  stmt.Bind (2, static_cast<int> (status));
  stmt.Bind (3, poster);

  if (worker.empty ())
    stmt.BindNull (4);
  else
    stmt.Bind (4, worker);

  stmt.Bind (5, reward);
  stmt.Bind (6, collateral);
  stmt.Bind (7, deadline);
  stmt.BindProto (8, data);

  stmt.Execute ();
}

JobsTable::Handle
JobsTable::CreateNew (const std::string& poster, const Amount reward,
                      const Amount collateral, const int64_t deadline)
{
  Handle j(new Job (db));

  j->status = Job::Status::OPEN;
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
  /* Overdue rows (deadline strictly below now) are routine, not an anomaly:
     deadlines fall between superblocks and the sweep only runs on
     superblocks, so less-or-equal picks up everything that became due since
     the previous sweep (the processing loop still asserts nothing from the
     future got in).

     Ordering by (deadline, id) is deterministic (id breaks ties, and it
     aliases the rowid the jobs_by_deadline index carries) and lets the
     query run straight off that index instead of sorting all due rows in a
     temporary B-tree.  */
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

/** Result type for the admission-cap COUNT queries.  */
struct CountResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, cnt, 1);
};

/** Runs a prepared COUNT query and returns its single value.  */
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
  /* The one count without a WHERE clause: SQLite serves it with its
     count optimisation over the smallest index (no per-row scan), and it
     is in any case bounded by the very cap it enforces (the board can
     never exceed the global cap plus the in-flight block's admissions).  */
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

/** Result type for the reserved-coins aggregate queries.  */
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

  /* Rewards escrowed by posters (over OPEN and ACCEPTED jobs alike).  */
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

  /* Collateral locked by workers on accepted jobs (worker is NULL while
     OPEN, so those rows are grouped under NULL and skipped below).  */
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
