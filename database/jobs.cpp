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
    type(Type::INVALID), status(Status::OPEN), faction(Faction::INVALID),
    reward(0), collateral(0),
    hasDeadline(false), deadline(0),
    linkedId(Database::EMPTY_ID),
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

  type = static_cast<Type> (res.Get<JobResult::type> ());
  status = static_cast<Status> (res.Get<JobResult::status> ());
  faction = GetNullableFactionFromColumn (res);
  poster = res.Get<JobResult::poster> ();

  if (res.IsNull<JobResult::worker> ())
    worker = "";
  else
    worker = res.Get<JobResult::worker> ();

  reward = res.Get<JobResult::reward> ();
  collateral = res.Get<JobResult::collateral> ();

  if (res.IsNull<JobResult::deadline> ())
    {
      hasDeadline = false;
      deadline = 0;
    }
  else
    {
      hasDeadline = true;
      deadline = res.Get<JobResult::deadline> ();
    }

  if (res.IsNull<JobResult::linked_id> ())
    linkedId = Database::EMPTY_ID;
  else
    linkedId = res.Get<JobResult::linked_id> ();

  if (res.IsNull<JobResult::linked_name> ())
    linkedName = "";
  else
    linkedName = res.Get<JobResult::linked_name> ();

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

  CHECK (type != Type::INVALID) << "Job " << id << " has no type set";
  CHECK (status != Status::INVALID) << "Job " << id << " has no status set";
  CHECK (!poster.empty ()) << "Job " << id << " has no poster set";
  CHECK_GE (reward, 0) << "Job " << id << " has negative reward";
  CHECK_GE (collateral, 0) << "Job " << id << " has negative collateral";

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `jobs`
      (`id`, `type`, `status`, `faction`, `poster`, `worker`,
       `reward`, `collateral`, `deadline`, `linked_id`, `linked_name`,
       `proto`)
      VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)
  )");

  stmt.Bind (1, id);
  stmt.Bind (2, static_cast<int> (type));
  stmt.Bind (3, static_cast<int> (status));
  BindFactionParameter (stmt, 4, faction);
  stmt.Bind (5, poster);

  if (worker.empty ())
    stmt.BindNull (6);
  else
    stmt.Bind (6, worker);

  stmt.Bind (7, reward);
  stmt.Bind (8, collateral);

  if (hasDeadline)
    stmt.Bind (9, deadline);
  else
    stmt.BindNull (9);

  if (linkedId == Database::EMPTY_ID)
    stmt.BindNull (10);
  else
    stmt.Bind (10, linkedId);

  if (linkedName.empty ())
    stmt.BindNull (11);
  else
    stmt.Bind (11, linkedName);

  stmt.BindProto (12, data);

  stmt.Execute ();
}

JobsTable::Handle
JobsTable::CreateNew (const Job::Type type, const Faction faction,
                      const std::string& poster,
                      const Amount reward, const Amount collateral)
{
  Handle j(new Job (db));

  j->type = type;
  j->status = Job::Status::OPEN;
  j->faction = faction;
  j->poster = poster;
  j->reward = reward;
  j->collateral = collateral;

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
  /* Standing jobs (NULL deadline) are excluded outright.  We fetch
     less-or-equal (rather than exact) so that any row with a deadline below
     the current timestamp -- which should never happen -- is still picked up
     and can be asserted on while processing.  */
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `jobs`
      WHERE `deadline` IS NOT NULL AND `deadline` <= ?1
      ORDER BY `id`
  )");
  stmt.Bind (1, now);
  return stmt.Query<JobResult> ();
}

Database::Result<JobResult>
JobsTable::QueryForLinkedId (const Database::IdT entity)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `jobs`
      WHERE `linked_id` = ?1
      ORDER BY `id`
  )");
  stmt.Bind (1, entity);
  return stmt.Query<JobResult> ();
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
