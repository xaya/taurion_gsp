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

#ifndef DATABASE_DATABASE_HPP
#define DATABASE_DATABASE_HPP

#include <xayagame/sqlitegame.hpp>

#include <glog/logging.h>

#include <google/protobuf/message.h>

#include <sqlite3.h>

#include <map>
#include <string>
#include <type_traits>

namespace pxd
{

/**
 * Basic class that is used to provide connectivity to the database
 * and related services provided by SQLiteGame (e.g. AutoId's and prepared
 * statements).
 *
 * The class is abstract, so that we can implement it both based on PXLogic
 * (SQLiteGame subclass) and directly for unit tests without the need to
 * have a SQLiteGame.
 */
class Database
{

protected:

  Database () = default;

  /**
   * Constructs an SQLite prepared statement based on the given SQL.  The
   * returned object is managed externally and may be cached.  It should just
   * be used to step through all results and then abandoned.
   */
  virtual sqlite3_stmt* PrepareStatement (const std::string& sql) = 0;

public:

  using IdT = xaya::SQLiteGame::IdT;
  static constexpr IdT EMPTY_ID = xaya::SQLiteGame::EMPTY_ID;

  template <typename T>
    class Result;
  class ResultType;
  class Statement;

  Database (const Database&) = delete;
  void operator= (const Database&) = delete;

  virtual ~Database () = default;

  /**
   * Returns the next auto-generated ID.  Unlike SQLiteGame, we only use
   * a single series for all IDs.  There is no harm in doing that, and it
   * avoids the risk of mixing up IDs if the same one can be, e.g. both
   * a character and an item.
   */
  virtual IdT GetNextId () = 0;

  /**
   * Prepares an SQL statement and returns the wrapper object.
   */
  Statement Prepare (const std::string& sql);

};

/**
 * Wrapper class around an sqlite3_stmt prepared statement.  It allows binding
 * of parameters including std::string and protocol buffers (to BLOBs).
 */
class Database::Statement
{

private:

  /** The Database this corresponds to.  */
  Database* db;

  /**
   * The underlying SQLite prepared statement.  As with the general method
   * for preparing statements, this is not owned by the instance (or rather,
   * it is only used temporarily and not freed when done).
   */
  sqlite3_stmt* stmt;

  /** Set to true when Execute or Query have been called.  */
  bool run = false;

  /**
   * Constructs an instance based on the given statement handle.  This is called
   * by Database::Prepare and not used directly.
   */
  explicit Statement (Database& d, sqlite3_stmt* s)
    : db(&d), stmt(s)
  {}

  friend class Database;

public:

  /* The object is movable but not copyable.  This is used to make code
     like the following work:

       Statement s = db.Prepare (...);
  */

  Statement (const Statement&) = delete;
  Statement& operator= (const Statement&) = delete;

  Statement (Statement&&) = default;
  Statement& operator= (Statement&&) = default;

  /**
   * Binds a parameter to the given data.  Strings are bound with an internal
   * copy made in SQLite.
   */
  template <typename T>
    void Bind (unsigned ind, const T& val);

  /**
   * Binds a null value to a parameter.
   */
  void BindNull (unsigned ind);

  /**
   * Binds a protocol buffer to a BLOB parameter.
   */
  void BindProto (unsigned ind, const google::protobuf::Message& msg);

  /**
   * Resets the statement so it can be used again with fresh bindings
   * and fresh execution from start.
   */
  void Reset ();

  /**
   * Executes the statement without expecting any results.  This is used for
   * statements other than SELECT.
   */
  void Execute ();

  /**
   * Executes the statement as SELECT and returns a handle for the resulting
   * database rows.
   */
  template <typename T>
    Result<T> Query ();

};

/**
 * Type specifying a kind of database result (e.g. is this a row of the
 * characters table?).  This class (or rather, subclasses of it) are used as
 * template parameter for Result<T>, and they should define appropriate
 * static methods.  It should not be instantiated.
 */
class Database::ResultType
{

public:

  ResultType () = delete;
  ResultType (const ResultType&) = delete;

};

/**
 * Wrapper around sqlite3_stmt, but taking care of reading results of a
 * query rather than binding values.  Results are "typed", where the type
 * indicates what kind of row this is (e.g. from the character table or
 * from accounts).
 */
template <typename T>
  class Database::Result
{

private:

  static_assert (std::is_base_of<ResultType, T>::value,
                 "Result type has an invalid type");

  /** The database this corresponds to.  */
  Database* db;

  /** The underlying sqlit3_stmt handle.  */
  sqlite3_stmt* stmt;

  /**
   * Whether or not the first row has already been read.  This is set when
   * Step() is called for the first time.  It is used to initialise some
   * state like the column-name-map.
   */
  bool initialised = false;

  /** Map of column names to indices.  */
  std::map<std::string, int> columnInd;

  /**
   * Constructs an instance based on the given statement handle.  This is called
   * by Statement::Query and not used directly.
   */
  explicit Result (Database& d, sqlite3_stmt* s)
    : db(&d), stmt(s)
  {}

  /**
   * Initialises the columnInd map based on the current result row associated
   * to stmt.
   */
  void BuildColumnMap ();

  /**
   * Returns the column index for the given name.  Also checks that the
   * column map has been initialised already.
   */
  int ColumnIndex (const std::string& name) const;

  friend class Statement;

public:

  /* The object is movable but not copyable.  This is used to make code
     like the following work:

       Result r = stmt.Query ();
  */

  Result (const Result<T>&) = delete;
  Result& operator= (const Result<T>&) = delete;

  Result (Result<T>&&) = default;
  Result& operator= (Result<T>&&) = default;

  /**
   * Tries to step to the next result.  Returns false if there is none.
   */
  bool Step ();

  /**
   * Extracts the column with the given name as type T.
   */
  template <typename C>
    C Get (const std::string& name) const;

  /**
   * Extracts a protocol buffer from the column with the given name.
   */
  void GetProto (const std::string& name, google::protobuf::Message& res) const;

  /**
   * Returns the underlying database handle.
   */
  Database&
  GetDatabase ()
  {
    return *db;
  }

};

} // namespace pxd

#include "database.tpp"

#endif // DATABASE_DATABASE_HPP
