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

#ifndef DATABASE_JOBS_HPP
#define DATABASE_JOBS_HPP

#include "amount.hpp"
#include "database.hpp"
#include "lazyproto.hpp"

#include "proto/jobs.pb.h"

#include <map>
#include <memory>
#include <string>

namespace pxd
{

/** Database result type for rows from the jobs table.  */
struct JobResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (int64_t, status, 2);
  RESULT_COLUMN (std::string, poster, 3);
  RESULT_COLUMN (std::string, worker, 4);
  RESULT_COLUMN (int64_t, reward, 5);
  RESULT_COLUMN (int64_t, collateral, 6);
  RESULT_COLUMN (int64_t, deadline, 7);
  RESULT_COLUMN (pxd::proto::JobData, proto, 8);
};

/**
 * Wrapper class around a job on the jobs board in the database.  Instances
 * should be obtained through JobsTable.  The scalar column fields can be
 * modified (status and worker on accept, deadline on a reaction-window
 * extension); the rest of the deal is held in the proto blob.
 */
class Job
{

public:

  /**
   * The lifecycle status of a job.  The numeric values match the `status`
   * database column.  Terminal transitions delete the row entirely.
   */
  enum class Status
  {
    INVALID = -1,
    OPEN = 0,
    ACCEPTED = 1,
  };

private:

  /** Database reference this belongs to.  */
  Database& db;

  /** The underlying ID in the database.  */
  Database::IdT id;

  /** The UniqueHandles tracker for this instance.  */
  Database::HandleTracker tracker;

  /** The lifecycle status.  */
  Status status;

  /** The account that posted the job.  */
  std::string poster;

  /** The account that accepted the job (empty while OPEN).  */
  std::string worker;

  /** The reward in vCHI paid on settlement.  */
  Amount reward;

  /** The collateral in vCHI locked by the worker.  */
  Amount collateral;

  /** The absolute consensus timestamp (seconds) at which the job expires.  */
  int64_t deadline;

  /** The rest of the deal (designated worker, arbiter, confirmations, ...).  */
  LazyProto<proto::JobData> data;

  /** Whether or not the column fields are dirty and need writing.  */
  bool dirtyFields;

  /**
   * Constructs a new instance with auto-generated ID meant to be inserted
   * into the database.
   */
  explicit Job (Database& d);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by a JobsTable.
   */
  explicit Job (Database& d, const Database::Result<JobResult>& res);

  friend class JobsTable;

public:

  /**
   * In the destructor, the underlying database is updated if there are any
   * modifications to send.
   */
  ~Job ();

  Job () = delete;
  Job (const Job&) = delete;
  void operator= (const Job&) = delete;

  Database::IdT
  GetId () const
  {
    return id;
  }

  Status
  GetStatus () const
  {
    return status;
  }

  void
  SetStatus (const Status s)
  {
    status = s;
    dirtyFields = true;
  }

  const std::string&
  GetPoster () const
  {
    return poster;
  }

  const std::string&
  GetWorker () const
  {
    return worker;
  }

  void
  SetWorker (const std::string& w)
  {
    worker = w;
    dirtyFields = true;
  }

  Amount
  GetReward () const
  {
    return reward;
  }

  Amount
  GetCollateral () const
  {
    return collateral;
  }

  int64_t
  GetDeadline () const
  {
    return deadline;
  }

  void
  SetDeadline (const int64_t d)
  {
    deadline = d;
    dirtyFields = true;
  }

  const proto::JobData&
  GetProto () const
  {
    return data.Get ();
  }

  proto::JobData&
  MutableProto ()
  {
    return data.Mutable ();
  }

};

/**
 * Utility class that handles querying the jobs table in the database and
 * should be used to obtain Job instances.
 */
class JobsTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to an instance.  */
  using Handle = std::unique_ptr<Job>;

  explicit JobsTable (Database& d)
    : db(d)
  {}

  JobsTable () = delete;
  JobsTable (const JobsTable&) = delete;
  void operator= (const JobsTable&) = delete;

  /**
   * Creates a new job (status OPEN, no worker) and returns the handle so the
   * proto payload can be filled in.  All the column fields are passed here so
   * that the row is always valid.
   */
  Handle CreateNew (const std::string& poster, Amount reward,
                    Amount collateral, int64_t deadline);

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<JobResult>& res);

  /**
   * Returns a handle for the given ID (or null if it doesn't exist).
   */
  Handle GetById (Database::IdT id);

  /**
   * Queries the database for all jobs on the board, ordered by ID.
   */
  Database::Result<JobResult> QueryAll ();

  /**
   * Queries for all jobs whose deadline is at or before the given (current)
   * consensus timestamp.  This is the expiry sweep; on the vast majority of
   * blocks it returns nothing and touches no rows.
   */
  Database::Result<JobResult> QueryForDeadline (int64_t now);

  /**
   * Row counts for the admission caps, each a cap-bounded COUNT over live
   * rows: the whole board, and one poster's jobs.
   */
  int64_t CountAll () const;
  int64_t CountForPoster (const std::string& poster) const;

  /**
   * Deletes the job with the given ID.  Used on every terminal transition
   * (settlement / cancel / expiry) to keep the table bounded.
   */
  void DeleteById (Database::IdT id);

  /**
   * Returns the total vCHI reserved by each account on the jobs board: the
   * sum of rewards over jobs they posted plus the sum of collateral over jobs
   * they accepted.  Mirrors DexOrderTable::GetReservedCoins so the two can be
   * merged into an account's balance.reserved.
   */
  std::map<std::string, Amount> GetReservedCoins () const;

};

} // namespace pxd

#endif // DATABASE_JOBS_HPP
