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

#include "database.hpp"

#include <glog/logging.h>

#include <memory>

namespace pxd
{

/* ************************************************************************** */

constexpr Database::IdT Database::EMPTY_ID;

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

/* ************************************************************************** */

namespace internal
{

template <>
  int64_t
  GetColumnValue<int64_t> (sqlite3_stmt* stmt, const int index)
{
  return sqlite3_column_int64 (stmt, index);
}

template <>
  bool
  GetColumnValue<bool> (sqlite3_stmt* stmt, const int index)
{
  return sqlite3_column_int (stmt, index);
}

template <>
  std::string
  GetColumnValue<std::string> (sqlite3_stmt* stmt, const int index)
{
  const unsigned char* str = sqlite3_column_text (stmt, index);
  return reinterpret_cast<const char*> (str);
}

} // namespace internal

/* ************************************************************************** */

} // namespace pxd
