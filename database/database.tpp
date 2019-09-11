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

#include <cstring>

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
  Database::Result<T>::Result (Database& d, sqlite3_stmt* s)
    : db(&d), stmt(s)
{
  columnInd.fill (MISSING_COLUMN);
}

template <typename T>
template <typename Col>
  int
  Database::Result<T>::ColumnIndex () const
{
  const int res = columnInd[Col::ID];
  if (res != MISSING_COLUMN)
    return res;

  const int num = sqlite3_column_count (stmt);
  for (int i = 0; i < num; ++i)
    {
      const char* name = sqlite3_column_name (stmt, i);
      if (std::strcmp (name, Col::NAME) == 0)
        {
          columnInd[Col::ID] = i;
          return i;
        }
    }

  LOG (FATAL) << "Column " << Col::NAME << " not returned by database";
}

template <typename T>
  bool
  Database::Result<T>::Step ()
{
  const int rc = sqlite3_step (stmt);
  if (rc == SQLITE_DONE)
    return false;

  CHECK_EQ (rc, SQLITE_ROW);
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
template <typename Col>
  typename Col::Type
  Database::Result<T>::Get () const
{
  return internal::GetColumnValue<typename Col::Type> (stmt,
                                                       ColumnIndex<Col> ());
}

template <typename T>
template <typename Col>
  void
  Database::Result<T>::GetProto (typename Col::Type& res) const
{
  const int ind = ColumnIndex<Col> ();

  const int len = sqlite3_column_bytes (stmt, ind);
  const void* bytes = sqlite3_column_blob (stmt, ind);

  CHECK (res.ParseFromArray (bytes, len));
}

} // namespace pxd
