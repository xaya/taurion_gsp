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

/* Template implementation code for database.hpp.  */

#include <glog/logging.h>

namespace pxd
{

template <typename T>
  Database::Result<T>
  Database::Statement::Query ()
{
  CHECK (!run) << "Database statement has already been run";
  run = true;
  return Result<T> (*db, stmt);
}

template <typename T>
  void
  Database::Result<T>::BuildColumnMap ()
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

template <typename T>
  int
  Database::Result<T>::ColumnIndex (const std::string& name) const
{
  CHECK (initialised);
  const auto mit = columnInd.find (name);
  CHECK (mit != columnInd.end ())
      << "Column name not in result set: " << name;
  return mit->second;
}

template <typename T>
  bool
  Database::Result<T>::Step ()
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

namespace internal
{

/**
 * Extracts a typed object from the SQLite statement.  This is used internally
 * to implement Result<T>::Get<C> accordingly.  The actual logic is in
 * certain specialisations below.
 */
template <typename C>
  C GetColumnValue (sqlite3_stmt* stmt, int index);

} // namespace internal

template <typename T>
template <typename C>
  C
  Database::Result<T>::Get (const std::string& name) const
{
  return internal::GetColumnValue<C> (stmt, ColumnIndex (name));
}

template <typename T>
  void
  Database::Result<T>::GetProto (const std::string& name,
                                 google::protobuf::Message& res) const
{
  const int ind = ColumnIndex (name);

  const int len = sqlite3_column_bytes (stmt, ind);
  const void* bytes = sqlite3_column_blob (stmt, ind);

  const std::string str(static_cast<const char*> (bytes), len);
  CHECK (res.ParseFromString (str));
}

} // namespace pxd
