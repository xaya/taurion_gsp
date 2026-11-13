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

#include "dbtest.hpp"

#include "moneysupply.hpp"
#include "schema.hpp"

#include <xayautil/hash.hpp>

#include <glog/logging.h>

namespace pxd
{

TestDatabase::TestDatabase ()
  : db("test", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY)
{
  SetDatabase (db);
}

Database::IdT
TestDatabase::GetNextId ()
{
  return nextId++;
}

Database::IdT
TestDatabase::GetLogId ()
{
  return nextLogId++;
}

DBTestWithSchema::DBTestWithSchema ()
{
  LOG (INFO) << "Setting up game-state schema in test database...";
  SetupDatabaseSchema (*db);

  MoneySupply ms(db);
  ms.InitialiseDatabase ();
}

DBTestWithUndo::DBTestWithUndo ()
{
  (*db).Execute (R"(
    CREATE TABLE `xayagame_undo`
        (`hash` BLOB PRIMARY KEY,
         `data` BLOB NOT NULL,
         `height` INTEGER NOT NULL);
  )");
}

xaya::uint256
DBTestWithUndo::Block (const std::function<void ()>& update)
{
  std::string changeset;
  (*db).AccessDatabase ([&] (sqlite3* h)
    {
      sqlite3_session* session;
      CHECK_EQ (sqlite3session_create (h, "main", &session), SQLITE_OK);
      CHECK_EQ (sqlite3session_attach (session, nullptr), SQLITE_OK);

      update ();

      int size;
      void* data;
      CHECK_EQ (sqlite3session_changeset (session, &size, &data), SQLITE_OK);
      changeset.assign (static_cast<const char*> (data), size);
      sqlite3_free (data);
      sqlite3session_delete (session);
    });

  ++height;
  const auto hash = xaya::SHA256::Hash (std::to_string (height));

  auto stmt = (*db).Prepare (R"(
    INSERT INTO `xayagame_undo` (`hash`, `data`, `height`)
      VALUES (?1, ?2, ?3)
  )");
  stmt.Bind (1, hash);
  stmt.BindBlob (2, changeset);
  stmt.Bind (3, height);
  stmt.Execute ();

  return hash;
}

xaya::uint256
DBTestWithUndo::EmptyBlock ()
{
  return Block ([] () {});
}

TemporaryDatabaseChanges::TemporaryDatabaseChanges (Database& d,
                                                    benchmark::State& s)
  : db(d), benchmarkState(s)
{
  benchmarkState.PauseTiming ();
  auto stmt = db.Prepare ("SAVEPOINT `TemporaryDatabaseChanges`");
  stmt.Execute ();
  benchmarkState.ResumeTiming ();
}

TemporaryDatabaseChanges::~TemporaryDatabaseChanges ()
{
  benchmarkState.PauseTiming ();
  auto stmt = db.Prepare ("ROLLBACK TO `TemporaryDatabaseChanges`");
  stmt.Execute ();
  benchmarkState.ResumeTiming ();
}

} // namespace pxd
