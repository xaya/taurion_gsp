/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#include "account.hpp"

namespace pxd
{

Account::Account (Database& d, const std::string& n, const Faction f)
  : db(d), name(n), faction(f), isNew(true)
{
  VLOG (1) << "Created instance for newly initialised account " << name;
  data.SetToDefault ();
  data.Mutable ().set_fame (100);
}

Account::Account (Database& d, const Database::Result<AccountResult>& res)
  : db(d), isNew(false)
{
  name = res.Get<AccountResult::name> ();
  faction = GetFactionFromColumn (res);
  data = res.GetProto<AccountResult::proto> ();

  VLOG (1) << "Created account instance for " << name << " from database";
}

Account::~Account ()
{
  if (!isNew && !data.IsDirty ())
    {
      VLOG (1) << "Account instance " << name << " is not dirty";
      return;
    }

  VLOG (1) << "Updating account " << name << " in the database";
  CHECK_GE (GetBalance (), 0);

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `accounts`
      (`name`, `faction`, `proto`)
      VALUES (?1, ?2, ?3)
  )");

  stmt.Bind (1, name);
  BindFactionParameter (stmt, 2, faction);
  stmt.BindProto (3, data);

  stmt.Execute ();
}

void
Account::AddBalance (const Amount val)
{
  Amount balance = data.Get ().balance ();
  balance += val;
  CHECK_GE (balance, 0);
  data.Mutable ().set_balance (balance);
}

AccountsTable::Handle
AccountsTable::CreateNew (const std::string& name, const Faction f)
{
  CHECK (GetByName (name) == nullptr)
      << "Account for " << name << " exists already";
  return Handle (new Account (db, name, f));
}

AccountsTable::Handle
AccountsTable::GetFromResult (const Database::Result<AccountResult>& res)
{
  return Handle (new Account (db, res));
}

AccountsTable::Handle
AccountsTable::GetByName (const std::string& name)
{
  auto stmt = db.Prepare ("SELECT * FROM `accounts` WHERE `name` = ?1");
  stmt.Bind (1, name);
  auto res = stmt.Query<AccountResult> ();

  if (!res.Step ())
    return nullptr;

  auto r = GetFromResult (res);
  CHECK (!res.Step ());
  return r;
}

Database::Result<AccountResult>
AccountsTable::QueryInitialised ()
{
  auto stmt = db.Prepare ("SELECT * FROM `accounts` ORDER BY `name`");
  return stmt.Query<AccountResult> ();
}

} // namespace pxd
