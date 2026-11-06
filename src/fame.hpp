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

#ifndef PXD_FAME_HPP
#define PXD_FAME_HPP

#include "context.hpp"

#include "database/account.hpp"
#include "database/character.hpp"
#include "database/damagelists.hpp"
#include "database/database.hpp"
#include "proto/combat.pb.h"

#include <map>
#include <string>

namespace pxd
{

/**
 * The main class handling computation and updates of fame.  This is notified
 * about kills by the combat logic, and then updates the fame accordingly.
 *
 * The actual fame update takes place in a virtual subroutine, so that this
 * can be mocked out for testing (or tested separately).
 */
class FameUpdater
{

private:

  /** A DamageLists instance we use for the updates during computation.  */
  DamageLists dl;

  /** Character table used for looking up owners.  */
  CharacterTable characters;

  /** Accounts table for updating fame and kills.  */
  AccountsTable accounts;

  /**
   * The delta computed in fame for given account names.  We compute this here
   * before applying it at the very end (in the destructor).  That allows us
   * to compute everything independent of the processing order, since the
   * computations themselves are done on the initial fame values.
   */
  std::map<std::string, int> deltas;

  /**
   * Owners of characters and handles of accounts looked up for the fame
   * update.  While a KillBatch is active, they are kept, so that each is
   * read and written only once for many kills.  Otherwise they are dropped
   * (and the accounts written) after each kill.
   */
  std::map<Database::IdT, std::string> ownerOf;
  std::map<std::string, AccountsTable::Handle> accountHandles;

  /** Whether a KillBatch is active.  */
  bool inBatch = false;

  /**
   * Returns the owner of the given character.
   */
  const std::string& GetOwner (Database::IdT id);

  /**
   * Returns the account with the given name.
   */
  Account& GetAccount (const std::string& name);

  /**
   * Drops the looked-up owners and accounts, writing back the accounts.
   */
  void ClearLookups ();

  /**
   * Computes the "fame level" of a player.  This is used to determine who
   * gets fame (namely those max one level above/below).  This is an int so
   * that we can safely compute differences.
   */
  static int GetLevel (unsigned fame);

  friend class FameLevelTests;
  friend class FameTests;

protected:

  /**
   * Updates fame accordingly for the given kill.  This is the main internal
   * routine handling fame computation, which holds the actual logic.
   *
   * It is virtual so that it can be mocked for testing.
   */
  virtual void UpdateForKill (Database::IdT victim,
                              const DamageLists::Attackers& attackers);

public:

  explicit FameUpdater (Database& db, const Context& ctx)
    : dl(db, ctx.Height ()), characters(db), accounts(db)
  {}

  virtual ~FameUpdater ();

  FameUpdater () = delete;
  FameUpdater (const FameUpdater&) = delete;
  void operator= (const FameUpdater&) = delete;

  /**
   * Returns a DamageLists instance for the current block.
   */
  DamageLists&
  GetDamageLists ()
  {
    return dl;
  }

  /**
   * Updates fame when the given fighter target has been killed.
   */
  void UpdateForKill (const proto::TargetId& target);

  class KillBatch;

};

/**
 * While an instance exists, the accounts and character owners that a
 * FameUpdater looks up for kills are kept, and the accounts are only written
 * back when it is destroyed.  Many kills (e.g. in a battle) thus read each of
 * them only once.  Nothing else may access the accounts in the meantime.
 */
class FameUpdater::KillBatch
{

private:

  FameUpdater& fame;

public:

  explicit KillBatch (FameUpdater& f);
  ~KillBatch ();

  KillBatch () = delete;
  KillBatch (const KillBatch&) = delete;
  void operator= (const KillBatch&) = delete;

};

} // namespace pxd

#endif // PXD_FAME_HPP
