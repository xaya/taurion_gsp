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

#include "database.hpp"

#include <glog/logging.h>

#include <memory>

namespace pxd
{

/* ************************************************************************** */

Database::Statement
Database::Prepare (const std::string& sql)
{
  return Statement (*this, PrepareStatement (sql));
}

/* ************************************************************************** */

void
Database::Statement::Reset ()
{
  sqlite3_reset (stmt);
  CHECK_EQ (sqlite3_clear_bindings (stmt), SQLITE_OK);
  run = false;
}

void
Database::Statement::Execute ()
{
  CHECK (!run) << "Database statement has already been run";
  run = true;
  CHECK_EQ (sqlite3_step (stmt), SQLITE_DONE);
}

Database::Result
Database::Statement::Query (const std::string& name)
{
  CHECK (!run) << "Database statement has already been run";
  run = true;
  return Result (*db, name, stmt);
}

template <>
  void
  Database::Statement::Bind<int32_t> (const unsigned ind, const int32_t& val)
{
  CHECK (!run);
  CHECK_EQ (sqlite3_bind_int (stmt, ind, val), SQLITE_OK);
}

template <>
  void
  Database::Statement::Bind<int64_t> (const unsigned ind, const int64_t& val)
{
  CHECK (!run);
  CHECK_EQ (sqlite3_bind_int64 (stmt, ind, val), SQLITE_OK);
}

template <>
  void
  Database::Statement::Bind<int16_t> (const unsigned ind, const int16_t& val)
{
  Bind<int32_t> (ind, val);
}

template <>
  void
  Database::Statement::Bind<uint64_t> (const unsigned ind, const uint64_t& val)
{
  CHECK_LE (val, std::numeric_limits<int64_t>::max ());
  Bind<int64_t> (ind, val);
}

template <>
  void
  Database::Statement::Bind<uint32_t> (const unsigned ind, const uint32_t& val)
{
  Bind<uint64_t> (ind, val);
}

template <>
  void
  Database::Statement::Bind<bool> (const unsigned ind, const bool& val)
{
  CHECK (!run);
  CHECK_EQ (sqlite3_bind_int (stmt, ind, val), SQLITE_OK);
}

template <>
  void
  Database::Statement::Bind<std::string> (const unsigned ind,
                                          const std::string& val)
{
  CHECK (!run);
  CHECK_EQ (sqlite3_bind_text (stmt, ind, &val[0], val.size (),
                               SQLITE_TRANSIENT),
            SQLITE_OK);
}

void
Database::Statement::BindNull (const unsigned ind)
{
  CHECK (!run);
  CHECK_EQ (sqlite3_bind_null (stmt, ind), SQLITE_OK);
}

void
Database::Statement::BindProto (const unsigned ind,
                                const google::protobuf::Message& msg)
{
  CHECK (!run);

  std::string str;
  CHECK (msg.SerializeToString (&str));

  CHECK_EQ (sqlite3_bind_blob (stmt, ind, &str[0], str.size (),
                               SQLITE_TRANSIENT),
            SQLITE_OK);
}

/* ************************************************************************** */

void
Database::Result::BuildColumnMap ()
{
  CHECK (columnInd.empty ());
  const int num = sqlite3_column_count (stmt);
  for (int i = 0; i < num; ++i)
    {
      const std::string name = sqlite3_column_name (stmt, i);
      columnInd.emplace (name, i);
    }
  CHECK_EQ (columnInd.size (), num);
}

int
Database::Result::ColumnIndex (const std::string& name) const
{
  CHECK (initialised);
  const auto mit = columnInd.find (name);
  CHECK (mit != columnInd.end ())
      << "Column name not in result set: " << name;
  return mit->second;
}

bool
Database::Result::Step ()
{
  const int rc = sqlite3_step (stmt);
  if (rc == SQLITE_DONE)
    return false;

  CHECK_EQ (rc, SQLITE_ROW);

  if (!initialised)
    {
      BuildColumnMap ();
      initialised = true;
    }

  return true;
}

template <>
  int64_t
  Database::Result::Get<int64_t> (const std::string& name) const
{
  return sqlite3_column_int64 (stmt, ColumnIndex (name));
}

template <>
  bool
  Database::Result::Get<bool> (const std::string& name) const
{
  return sqlite3_column_int (stmt, ColumnIndex (name));
}

template <>
  std::string
  Database::Result::Get<std::string> (const std::string& name) const
{
  const unsigned char* str = sqlite3_column_text (stmt, ColumnIndex (name));
  return reinterpret_cast<const char*> (str);
}

void
Database::Result::GetProto (const std::string& name,
                            google::protobuf::Message& res) const
{
  const int ind = ColumnIndex (name);
  const int len = sqlite3_column_bytes (stmt, ind);
  const void* bytes = sqlite3_column_blob (stmt, ind);

  const std::string str(static_cast<const char*> (bytes), len);
  CHECK (res.ParseFromString (str));
}

/* ************************************************************************** */

} // namespace pxd
