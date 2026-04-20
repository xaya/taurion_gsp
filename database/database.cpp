/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2026  Autonomous Worlds Ltd

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

#include "database.hpp"

#include <glog/logging.h>

namespace pxd
{

constexpr Database::IdT Database::EMPTY_ID;

void
Database::SetDatabase (xaya::SQLiteDatabase& d)
{
  CHECK (db == nullptr) << "Database has already been set";
  db = &d;
}

Database::Statement
Database::Prepare (const std::string& sql)
{
  CHECK (db != nullptr) << "Database has not been set";
  return Statement (*this, db->Prepare (sql));
}

namespace
{

/**
 * Database result for the superblocks table.
 */
struct SuperBlocksResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, height, 1);
  RESULT_COLUMN (int64_t, time, 2);
};

} // anonymous namespace

bool
Database::LastSuperBlock (unsigned& height, int64_t& timestamp) const
{
  auto stmt = const_cast<Database*> (this)->Prepare (R"(
    SELECT `height`, `time`
      FROM `superblocks`
      ORDER BY `height` DESC
      LIMIT 1
  )");
  auto res = stmt.Query<SuperBlocksResult> ();

  if (!res.Step ())
    return false;

  height = res.Get<SuperBlocksResult::height> ();
  timestamp = res.Get<SuperBlocksResult::time> ();

  CHECK (!res.Step ());

  return true;
}

void
Database::SetSuperBlock (const unsigned height, const int64_t timestamp)
{
  /* We only need to keep the current superblock, and can remove the previous
     one from the table.  */
  Prepare ("DELETE FROM `superblocks`").Execute ();

  auto stmt = Prepare (R"(
    INSERT INTO `superblocks`
      (`height`, `time`)
      VALUES (?1, ?2)
  )");
  stmt.Bind (1, height);
  stmt.Bind (2, timestamp);
  stmt.Execute ();
}

void
Database::Statement::Reset ()
{
  CHECK (!queried) << "SELECT statements can't be reset";
  sqlite3_clear_bindings (*stmt);
  stmt.Reset ();
  executed = false;
}

void
Database::Statement::Execute ()
{
  CHECK (!executed && !queried) << "Database statement has already been run";
  executed = true;
  stmt.Execute ();
}

template <>
  void
  Database::Statement::Bind<int16_t> (const unsigned ind, const int16_t& val)
{
  Bind<int64_t> (ind, val);
}

template <>
  void
  Database::Statement::Bind<int32_t> (const unsigned ind, const int32_t& val)
{
  Bind<int64_t> (ind, val);
}

} // namespace pxd
