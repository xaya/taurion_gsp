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

#ifndef PXD_JOBS_HPP
#define PXD_JOBS_HPP

#include "context.hpp"

#include "database/account.hpp"
#include "database/amount.hpp"
#include "database/database.hpp"
#include "database/jobs.hpp"

#include <json/json.h>

#include <memory>

namespace pxd
{

/*
   A job is a deal between two accounts with the coins held in escrow.  The
   poster locks a reward, and the worker who accepts the job may have to lock
   collateral.  If both confirm, the worker is paid.  If one of them disputes,
   the arbiter that the poster named decides which percentage of the work was
   done.  What is still open when the deadline passes is settled then.

   Job moves are processed for confirmed blocks only (not for pending moves),
   since they depend on the block timestamp.
*/

/**
 * The context and database tables that job operations work with.
 */
struct JobContext
{
  const Context& ctx;
  AccountsTable& accounts;
  JobsTable& jobs;
};

/**
 * One operation on the job board, which is one element of the "j" array
 * in a move.
 */
class JobOperation
{

protected:

  JobContext jc;

  /** The account triggering the operation.  */
  Account& account;

  JobOperation (Account& a, const JobContext& c)
    : jc(c), account(a)
  {}

public:

  virtual ~JobOperation () = default;

  /**
   * Returns true if the operation is valid in the current game state.
   */
  virtual bool IsValid () const = 0;

  /**
   * Executes the operation.  It must be valid.
   */
  virtual void Execute () = 0;

  /**
   * Tries to parse an operation from JSON.  Returns null if the format
   * is invalid, or if the account has the empty name (which cannot take
   * part in jobs).
   */
  static std::unique_ptr<JobOperation> Parse (Account& acc,
                                              const Json::Value& data,
                                              const JobContext& jc);

};

/**
 * Settles and removes all jobs that have reached their deadline.  This is
 * done on superblocks.  All of them are processed at once, so that each
 * job is paid out or refunded (and makes room for a new one) with the first
 * superblock after its deadline.  The work for that is bounded by the number
 * of jobs, which is limited when they are posted (max_live_jobs in the
 * config).
 */
void ExpireJobs (Database& db, const Context& ctx);

/**
 * Checks that the poster and worker of each job are initialised accounts.
 */
void ValidateJobs (Database& db);

} // namespace pxd

#endif // PXD_JOBS_HPP
