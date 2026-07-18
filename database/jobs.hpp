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
#include "faction.hpp"
#include "lazyproto.hpp"

#include "proto/jobs.pb.h"

#include <map>
#include <memory>
#include <string>

namespace pxd
{

/**
 * Database result type for rows from the jobs table.  The faction column is
 * inherited from ResultWithFaction; here it is *nullable* (a NULL audience
 * faction means "all factions"), so it is read with
 * GetNullableFactionFromColumn.
 */
struct JobResult : public ResultWithFaction
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (int64_t, type, 2);
  RESULT_COLUMN (int64_t, status, 3);
  RESULT_COLUMN (std::string, poster, 4);
  RESULT_COLUMN (std::string, worker, 5);
  RESULT_COLUMN (int64_t, reward, 6);
  RESULT_COLUMN (int64_t, collateral, 7);
  RESULT_COLUMN (int64_t, deadline, 8);
  RESULT_COLUMN (int64_t, linked_id, 9);
  RESULT_COLUMN (std::string, linked_name, 10);
  RESULT_COLUMN (pxd::proto::JobData, proto, 11);
};

/**
 * Why a job left the board -- the `outcome` column of the job_history table.
 * The numeric values are consensus-relevant (never renumber).
 */
enum class JobOutcome
{
  INVALID = -1,
  /** The worker delivered / held the term: reward paid, counters bumped.  */
  COMPLETED = 0,
  /** The accepted worker missed or lost it: collateral forfeited.  */
  FAILED = 1,
  /** The poster pulled an open post: escrow refunded.  */
  CANCELLED = 2,
  /** Expired or voided through neither party's fault: refunds all round.  */
  VOID = 3,
  /** A standing pool fully consumed by qualifying kills.  */
  DRAINED = 4,
};

/**
 * Database result type for rows from the job_history table.  Same faction
 * handling as JobResult.
 */
struct JobHistoryResult : public ResultWithFaction
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (int64_t, type, 2);
  RESULT_COLUMN (int64_t, outcome, 3);
  RESULT_COLUMN (int64_t, settled_height, 4);
  RESULT_COLUMN (int64_t, settled_time, 5);
  RESULT_COLUMN (std::string, poster, 6);
  RESULT_COLUMN (std::string, worker, 7);
  RESULT_COLUMN (int64_t, reward, 8);
  RESULT_COLUMN (int64_t, collateral, 9);
  RESULT_COLUMN (int64_t, deadline, 10);
  RESULT_COLUMN (int64_t, linked_id, 11);
  RESULT_COLUMN (std::string, linked_name, 12);
  RESULT_COLUMN (pxd::proto::JobData, proto, 13);
};

/**
 * Wrapper class around a job on the jobs board in the database.  Instances
 * should be obtained through JobsTable.  The scalar column fields can be
 * modified (status on accept, worker on accept, deadline on notice-cancel,
 * ...); the type-specific data is held in the proto blob.
 */
class Job
{

public:

  /**
   * The kind of job.  The numeric values match the `type` database column
   * and are consensus-relevant (never renumber).
   */
  enum class Type
  {
    INVALID = -1,
    TRANSPORT = 0,
    HAUL = 1,
    WANTED = 2,
    PROTECT = 3,
    DESTROY = 4,
    ESCORT = 5,
    BODYGUARD = 6,
    PATROL = 7,
    RENTAL = 8,
    AD = 9,
    TOLL = 10,
  };

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

  /** The job kind.  */
  Type type;

  /** The lifecycle status.  */
  Status status;

  /** The audience faction (INVALID means the NULL "all factions" audience).  */
  Faction faction;

  /** The account that posted the job.  */
  std::string poster;

  /** The account that accepted the job (empty while OPEN).  */
  std::string worker;

  /** The reward in vCHI paid on settlement.  */
  Amount reward;

  /** The collateral in vCHI locked by the worker.  */
  Amount collateral;

  /**
   * Whether a deadline is set.  False = the standing class (never swept);
   * a NULL deadline column must never be read as 0.
   */
  bool hasDeadline;

  /** The absolute consensus timestamp (seconds) at which the job expires.  */
  int64_t deadline;

  /** The linked entity whose death the job is tied to (EMPTY_ID if none).  */
  Database::IdT linkedId;

  /** The target account for a name-scoped job (empty if none).  */
  std::string linkedName;

  /** Type-specific proto data (the designated worker, the manifest, ...).  */
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

  Type
  GetType () const
  {
    return type;
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

  Faction
  GetFaction () const
  {
    return faction;
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

  /**
   * Updates the reward column.  Used only by the wanted-bounty pool, where
   * the column tracks the REMAINING (un-paid) escrow as tranches pay out,
   * keeping balance.reserved a plain SUM.
   */
  void
  SetReward (const Amount r)
  {
    reward = r;
    dirtyFields = true;
  }

  Amount
  GetCollateral () const
  {
    return collateral;
  }

  /** Returns true if the job has a deadline (i.e. is not standing).  */
  bool
  HasDeadline () const
  {
    return hasDeadline;
  }

  /** Returns the deadline; must only be called when HasDeadline().  */
  int64_t
  GetDeadline () const
  {
    CHECK (hasDeadline) << "Job " << id << " has no deadline";
    return deadline;
  }

  void
  SetDeadline (const int64_t d)
  {
    hasDeadline = true;
    deadline = d;
    dirtyFields = true;
  }

  Database::IdT
  GetLinkedId () const
  {
    return linkedId;
  }

  void
  SetLinkedId (const Database::IdT l)
  {
    linkedId = l;
    dirtyFields = true;
  }

  const std::string&
  GetLinkedName () const
  {
    return linkedName;
  }

  void
  SetLinkedName (const std::string& n)
  {
    linkedName = n;
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
 * Read-only view of one settled job from the job_history table.  Exposes the
 * same getter surface as Job for the fields both share (so the JSON
 * conversion is one shared template), plus the settlement metadata.
 */
class JobHistoryEntry
{

private:

  Database::IdT id;
  Job::Type type;
  JobOutcome outcome;
  unsigned settledHeight;
  int64_t settledTime;
  Faction faction;
  std::string poster;
  std::string worker;
  Amount reward;
  Amount collateral;
  bool hasDeadline;
  int64_t deadline;
  Database::IdT linkedId;
  std::string linkedName;
  LazyProto<proto::JobData> data;

  explicit JobHistoryEntry (const Database::Result<JobHistoryResult>& res);

  friend class JobsTable;

public:

  JobHistoryEntry () = delete;
  JobHistoryEntry (const JobHistoryEntry&) = delete;
  void operator= (const JobHistoryEntry&) = delete;

  Database::IdT GetId () const { return id; }
  Job::Type GetType () const { return type; }
  JobOutcome GetOutcome () const { return outcome; }
  unsigned GetSettledHeight () const { return settledHeight; }
  int64_t GetSettledTime () const { return settledTime; }
  Faction GetFaction () const { return faction; }
  const std::string& GetPoster () const { return poster; }
  const std::string& GetWorker () const { return worker; }
  Amount GetReward () const { return reward; }
  Amount GetCollateral () const { return collateral; }
  bool HasDeadline () const { return hasDeadline; }

  int64_t
  GetDeadline () const
  {
    CHECK (hasDeadline) << "History row " << id << " has no deadline";
    return deadline;
  }

  Database::IdT GetLinkedId () const { return linkedId; }
  const std::string& GetLinkedName () const { return linkedName; }
  const proto::JobData& GetProto () const { return data.Get (); }

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
   * Creates a new job (status OPEN, no worker, no deadline / linked entity yet)
   * and returns the handle so the deadline, linked entity and type-specific
   * proto payload can be filled in.  The always-required column fields are
   * passed here so that the row is always valid.
   */
  Handle CreateNew (Job::Type type, Faction faction, const std::string& poster,
                    Amount reward, Amount collateral);

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<JobResult>& res);

  /**
   * Returns a handle for the given ID (or null if it doesn't exist).
   */
  Handle GetById (Database::IdT id);

  /**
   * Queries the database for all jobs in the entire game world, ordered by
   * ID.  Used only by the full game-state export and internal validation;
   * every RPC-reachable jobs read pages through the capped QueryPage.
   */
  Database::Result<JobResult> QueryAll ();

  /**
   * Queries a hard-capped page of the live jobs board, ordered by ID.
   * `afterId` is an exclusive continuation cursor (0 = from the start); at
   * most `limit` rows are returned, itself clamped to MAX_PAGE (limit <= 0
   * uses the cap).  Callers page by passing the last returned row's ID.
   */
  Database::Result<JobResult> QueryPage (Database::IdT afterId = 0,
                                         int limit = 0);

  /**
   * Queries for all non-standing jobs whose deadline is at or before the given
   * (current) consensus timestamp.  This is the expiry sweep; standing jobs
   * (NULL deadline) are excluded, and on the vast majority of blocks it
   * returns nothing and touches no rows.
   */
  Database::Result<JobResult> QueryForDeadline (int64_t now);

  /**
   * Queries for all jobs linked to the given entity ID.  Used by the kill-hook
   * when a linked entity (e.g. a transport destination) is destroyed.
   */
  Database::Result<JobResult> QueryForLinkedId (Database::IdT entity);

  /**
   * Queries for all jobs whose linked_name is the given account -- the
   * wanted-bounties on that name.  Used by the kill attribution when one of
   * the account's characters dies.
   */
  Database::Result<JobResult> QueryForLinkedName (const std::string& name);

  /**
   * Returns whether ANY account is under an active bounty (a non-NULL
   * linked_name exists): a single covering-index existence probe, no row
   * enumeration.  The kill attribution (run in the superblock damage phase)
   * checks this once and, only if true, issues an indexed per-dead-owner
   * probe -- so the per-superblock cost scales with the deaths, never with
   * the number of dormant bounty targets on the board.
   */
  bool HasActiveBountyNames () const;

  /**
   * Row counts for the admission caps, each an indexed (or cap-bounded)
   * COUNT over live rows: the whole board, one poster's jobs, the jobs
   * linked to one entity, and the wanted pools on one target name.
   */
  int64_t CountAll () const;
  int64_t CountForPoster (const std::string& poster) const;
  int64_t CountForLinkedId (Database::IdT entity) const;
  int64_t CountForLinkedName (const std::string& name) const;

  /**
   * Deletes the job with the given ID.  Used on every terminal transition
   * (fulfil / cancel / expire / void) to keep the table bounded.
   */
  void DeleteById (Database::IdT id);

  /**
   * Writes the settled-jobs history row for a job that is about to be
   * deleted by a terminal transition, snapshotting its final state together
   * with the outcome and the settling block's height / consensus timestamp.
   * Every DeleteById on a settlement path must be preceded by this.
   */
  void WriteHistory (const Job& job, JobOutcome outcome,
                     unsigned settledHeight, int64_t settledTime);

  /**
   * Returns an entry for the given history result row.
   */
  std::unique_ptr<JobHistoryEntry>
      GetFromResult (const Database::Result<JobHistoryResult>& res);

  /**
   * Hard cap on rows returned by a single paged query (QueryPage /
   * QueryHistory and their RPCs), so no single response is unbounded;
   * callers page past it with the respective cursor.
   */
  static constexpr int MAX_PAGE = 2000;

  /**
   * Queries the settled-jobs history, ordered by settlement time then ID.
   * `fromTime` is an inclusive lower bound on settled_time (0 = everything
   * within retention).  `(afterTime, afterId)` is an exclusive continuation
   * cursor over the (settled_time, id) tuple (0,0 = from the start).  At most
   * `limit` rows are returned, itself clamped to MAX_PAGE (limit <= 0
   * uses the cap).  Callers page by passing the last returned row's
   * (settled_time, id) as the next cursor.
   */
  Database::Result<JobHistoryResult> QueryHistory (int64_t fromTime,
                                                   int64_t afterTime = 0,
                                                   int64_t afterId = 0,
                                                   int limit = 0);

  /**
   * Deletes at most `batch` history rows settled strictly before the cutoff
   * timestamp, oldest first (ordered by settled_time then id, so every node
   * deletes the same rows): the deterministic retention prune, run by the
   * (superblock-only) expiry sweep with cutoff = now -
   * params.jobs_history_retention.  The batch bound keeps one huge cohort
   * ageing out from forcing an unbounded single-statement delete; the
   * remainder drains on subsequent sweeps (history is display-only, so the
   * delay reopens no gameplay inputs).  batch <= 0 means unbounded and is
   * for tests only; the argument is deliberately not defaulted so the
   * production caller cannot silently omit its roconfig batch.
   */
  void PruneHistory (int64_t cutoff, int64_t batch);

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
