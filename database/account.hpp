/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#ifndef DATABASE_ACCOUNT_HPP
#define DATABASE_ACCOUNT_HPP

#include "database.hpp"

#include <memory>
#include <string>

namespace pxd
{

/**
 * Database result type for rows from the accounts table.
 */
class AccountResult : public Database::ResultType
{};

/**
 * Wrapper class around the state of one Xaya account (name) in the database.
 * Instantiations of this class should be made through the AccountsTable.
 */
class Account
{

private:

  /** Database reference this belongs to.  */
  Database& db;

  /** The Xaya name of this account.  */
  std::string name;

  /** The account's number of kills.  */
  unsigned kills;

  /** The account's fame value.  */
  unsigned fame;

  /**
   * Set to true if any modification has been made and we need to write
   * the changes back to the database in the destructor.
   */
  bool dirty;

  /**
   * Constructs an instance with "default / empty" data for the given name.
   */
  explicit Account (Database& d, const std::string& n);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by an AccountsTable.
   */
  explicit Account (Database& d, const Database::Result<AccountResult>& res);

  friend class AccountsTable;

public:

  /**
   * In the destructor, the underlying database is updated if there are any
   * modifications to send.
   */
  ~Account ();

  Account () = delete;
  Account (const Account&) = delete;
  void operator= (const Account&) = delete;

  /* Accessor methods.  */

  const std::string&
  GetName () const
  {
    return name;
  }

  unsigned
  GetKills () const
  {
    return kills;
  }

  void
  SetKills (const unsigned val)
  {
    dirty = true;
    kills = val;
  }

  unsigned
  GetFame () const
  {
    return fame;
  }

  void
  SetFame (const unsigned val)
  {
    dirty = true;
    fame = val;
  }

};

/**
 * Utility class that handles querying the accounts table in the database and
 * should be used to obtain Account instances.
 */
class AccountsTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to an account instance.  */
  using Handle = std::unique_ptr<Account>;

  explicit AccountsTable (Database& d)
    : db(d)
  {}

  AccountsTable () = delete;
  AccountsTable (const AccountsTable&) = delete;
  void operator= (const AccountsTable&) = delete;

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<AccountResult>& res);

  /**
   * Returns the account with the given name.
   */
  Handle GetByName (const std::string& name);

  /**
   * Queries the database for all accounts with (potentially) non-empty
   * data stored.  Returns a result set that can be used together with
   * GetFromResult.
   */
  Database::Result<AccountResult> QueryNonTrivial ();

};

} // namespace pxd

#endif // DATABASE_ACCOUNT_HPP
