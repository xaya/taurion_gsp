/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#include "amount.hpp"
#include "database.hpp"
#include "faction.hpp"
#include "lazyproto.hpp"

#include "proto/account.pb.h"

#include <memory>
#include <string>

namespace pxd
{

/**
 * Database result type for rows from the accounts table.
 */
struct AccountResult : public ResultWithFaction
{
  RESULT_COLUMN (std::string, name, 1);
  RESULT_COLUMN (pxd::proto::Account, proto, 2);
};

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

  /** UniqueHandles tracker for this instance.  */
  Database::HandleTracker tracker;

  /** The faction of this account.  May be INVALID if not yet initialised.  */
  Faction faction;

  /** General proto data.  */
  LazyProto<proto::Account> data;

  /** Whether or not this is dirty in the fields (like faction).  */
  bool dirtyFields;

  /**
   * Constructs an instance with "default / empty" data for the given name
   * and not-yet-set faction.
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

  const std::string&
  GetName () const
  {
    return name;
  }

  Faction
  GetFaction () const
  {
    return faction;
  }

  /**
   * Sets the faction.  Is only possible once, i.e. if the faction is so far
   * not set.
   */
  void SetFaction (Faction f);

  const proto::Account&
  GetProto () const
  {
    return data.Get ();
  }

  proto::Account&
  MutableProto ()
  {
    return data.Mutable ();
  }

  bool
  IsInitialised () const
  {
    return faction != Faction::INVALID;
  }

  /**
   * Updates the account balance by the given (signed) amount.  This should
   * be used instead of manually editing the proto, so that there is a single
   * place that controls all balance updates.
   */
  void AddBalance (Amount val);

  Amount
  GetBalance () const
  {
    return data.Get ().balance ();
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
   * Creates a new entry in the database for the given name.
   * Calling this method for a name that already has an account is an error.
   */
  Handle CreateNew (const std::string& name);

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<AccountResult>& res);

  /**
   * Returns the account with the given name.
   */
  Handle GetByName (const std::string& name);

  /**
   * Queries the database for all accounts, including uninitialised ones.
   */
  Database::Result<AccountResult> QueryAll ();

  /**
   * Queries the database for all accounts which have been initialised yet
   * with a faction.  Returns a result set that can be used together with
   * GetFromResult.
   */
  Database::Result<AccountResult> QueryInitialised ();

};

} // namespace pxd

#endif // DATABASE_ACCOUNT_HPP
