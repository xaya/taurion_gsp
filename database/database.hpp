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

#ifndef DATABASE_DATABASE_HPP
#define DATABASE_DATABASE_HPP

#include "lazyproto.hpp"

#include <xayagame/sqlitegame.hpp>
#include <xayagame/sqlitestorage.hpp>

#include <google/protobuf/arena.h>
#include <google/protobuf/message.h>

#include <sqlite3.h>

#include <array>
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

private:

  /** Underlying SQLiteDatabase from libxayagame.  */
  xaya::SQLiteDatabase* db = nullptr;

  /** Protocol buffer arena used for protos extracted from the database.  */
  google::protobuf::Arena arena;

protected:

  Database () = default;

  /**
   * Sets the underlying SQLite database.  This must be called by subclasses
   * before using the Database instance in any form, and is just separate
   * from the constructor to allow composability in the structure of
   * subclasses.
   */
  void SetDatabase (xaya::SQLiteDatabase& d);

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
   * Returns the next auto-generated ID that should be used for things
   * that are not consensus relevant (and thus can be changed more easily).
   * For instance, as keys into "events / log" tables that are just written
   * and never read during the state transition.
   */
  virtual IdT GetLogId () = 0;

  /**
   * Prepares an SQL statement and returns the wrapper object.
   */
  Statement Prepare (const std::string& sql);

  /**
   * Gives access to the underlying libxayagame Database instance.
   */
  xaya::SQLiteDatabase&
  operator* ()
  {
    return *db;
  }

};

/**
 * Wrapper class around an SQLite prepared statement.  It allows binding
 * of parameters including std::string and protocol buffers (to BLOBs).
 */
class Database::Statement
{

private:

  /** The Database this corresponds to.  */
  Database* db;

  /** The underlying SQLite prepared statement.  */
  xaya::SQLiteDatabase::Statement stmt;

  /** Set to true when Execute has been called.  */
  bool executed = false;
  /** Set to true when Query has been called.  */
  bool queried = false;

  /**
   * Constructs an instance based on the given libxayagame statement.
   * This is called by Database::Prepare and not used directly.
   */
  explicit Statement (Database& d, xaya::SQLiteDatabase::Statement&& s)
    : db(&d), stmt(std::move (s))
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
  inline void
  BindNull (unsigned ind)
  {
    stmt.BindNull (ind);
  }

  /**
   * Binds a protocol buffer to a BLOB parameter.
   */
  template <typename Proto>
    void BindProto (unsigned ind, const LazyProto<Proto>& msg);

  /**
   * Resets the statement so it can be used again with fresh bindings
   * and fresh execution from start.  This can be used after calling Execute,
   * but not after Query.
   */
  void Reset ();

  /**
   * Executes the statement without expecting any results.  This is used for
   * statements other than SELECT.
   */
  void Execute ();

  /**
   * Executes the statement as SELECT and returns a handle for the resulting
   * database rows.  This transfers the libxayagame statement out into the
   * result handle, and leaves this instance unusable (not even Reset can
   * be used on it anymore).
   */
  template <typename T>
    Result<T> Query ();

};

/**
 * Type specifying a kind of database result (e.g. is this a row of the
 * characters table?).  This class (or rather, subclasses of it) are used as
 * template parameter for Result<T>, and they should define appropriate
 * static methods.  It should not be instantiated.
 *
 * Subclasses must define the columns that they accept using the
 * RESULT_COLUMN macro.
 */
struct Database::ResultType
{

  /**
   * Type used for IDs of columns.  Each column accessed in a result of
   * a certain type must have an ID, which is mapped to its string name
   * in the database query.  Then lookups of the column are done by that
   * ID, which is faster than looking up strings in a map.
   */
  using ColumnId = unsigned;

  /**
   * Maximum number of columns we support (namely in the range
   * 0..MAX_ID-1).
   */
  static constexpr ColumnId MAX_ID = 64;

  /**
   * Define a new column supported by this result set.  It must define
   * the SQL column name, a unique ColumnId number, and the type.
   */
#define RESULT_COLUMN(type, name, id) \
  struct name \
  { \
    using Type = type; \
    static constexpr const char* NAME = #name; \
    static constexpr ColumnId ID = id; \
    static_assert (ID >= 0 && ID < MAX_ID, "Column ID is too large"); \
  }

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

  /** Values used as SQLite column "index" when the column is not present.  */
  static constexpr int MISSING_COLUMN = -1;

  /** The database this corresponds to.  */
  Database* db;

  /** The underlying libxayagame statement.  */
  xaya::SQLiteDatabase::Statement stmt;

  /** Map of ColumnId values to the indices in the SQLite statement.  */
  mutable std::array<int, ResultType::MAX_ID> columnInd;

  /**
   * Constructs an instance based on the given statement handle.  This is called
   * by Statement::Query and not used directly.
   */
  explicit Result (Database& d, xaya::SQLiteDatabase::Statement&& s);

  /**
   * Returns the index for a column defined in the result type.  Fills it in
   * in columnInd if it is not yet set there (assuming the column's name
   * can be found in the SQLite result).
   */
  template <typename Col>
    int ColumnIndex () const;

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
  inline bool
  Step ()
  {
    return stmt.Step ();
  }

  /**
   * Checks if the given column is null.
   */
  template <typename Col>
    bool IsNull () const;

  /**
   * Extracts the column of the given type.
   */
  template <typename Col>
    typename Col::Type Get () const;

  /**
   * Extracts a protocol buffer from the column of the given type.
   */
  template <typename Col>
    LazyProto<typename Col::Type> GetProto () const;

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
