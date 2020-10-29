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

/* Template implementation code for database.hpp.  */

#include <glog/logging.h>

#include <cstring>

namespace pxd
{

template <typename T>
  Database::Result<T>
  Database::Statement::Query ()
{
  CHECK (!executed && !queried) << "Database statement has already been run";
  queried = true;
  return Result<T> (*db, std::move (stmt));
}

template <typename T>
  void
  Database::Statement::Bind (const unsigned ind, const T& val)
{
  CHECK (!executed && !queried);
  stmt.Bind (ind, val);
}

/* Specialisations for types not supported by libxayagame's Statement
   directly (with implementations in the .cpp file).  */
template <>
  void Database::Statement::Bind<int16_t> (unsigned ind, const int16_t& val);
template <>
  void Database::Statement::Bind<int32_t> (unsigned ind, const int32_t& val);

template <typename Proto>
  void
  Database::Statement::BindProto (const unsigned ind,
                                  const LazyProto<Proto>& msg)
{
  CHECK (!executed && !queried);
  const std::string& str = msg.GetSerialised ();
  stmt.BindBlob (ind, str);
}

template <typename T>
  Database::Result<T>::Result (Database& d, xaya::SQLiteDatabase::Statement&& s)
    : db(&d), stmt(std::move (s))
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

  const int num = sqlite3_column_count (stmt.ro ());
  for (int i = 0; i < num; ++i)
    {
      const char* name = sqlite3_column_name (stmt.ro (), i);
      if (std::strcmp (name, Col::NAME) == 0)
        {
          columnInd[Col::ID] = i;
          return i;
        }
    }

  LOG (FATAL) << "Column " << Col::NAME << " not returned by database";
}

template <typename T>
template <typename Col>
  bool
  Database::Result<T>::IsNull () const
{
  return stmt.IsNull (ColumnIndex<Col> ());
}

template <typename T>
template <typename Col>
  typename Col::Type
  Database::Result<T>::Get () const
{
  return stmt.Get<typename Col::Type> (ColumnIndex<Col> ());
}

template <typename T>
template <typename Col>
  LazyProto<typename Col::Type>
  Database::Result<T>::GetProto () const
{
  const int ind = ColumnIndex<Col> ();
  return LazyProto<typename Col::Type> (stmt.GetBlob (ind));
}

} // namespace pxd
