#ifndef DATABASE_DATABASE_HPP
#define DATABASE_DATABASE_HPP

#include <xayagame/sqlitegame.hpp>

#include <glog/logging.h>

#include <google/protobuf/message.h>

#include <sqlite3.h>

#include <map>
#include <string>

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

  class Result;
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
   *
   * The "name" string is associated with the Result and can be retrieved from
   * it later on.  This can be used to verify that a Result object does indeed
   * match an expected query (e.g. it could be set to the table name from
   * which the query is made).
   */
  Result Query (const std::string& name = "");

};

/**
 * Wrapper around sqlite3_stmt, but taking care of reading results of a
 * query rather than binding values.
 */
class Database::Result
{

private:

  /** The database this corresponds to.  */
  Database* db;

  /** The "name" associated with this result when the query was made.  */
  std::string name;

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
  explicit Result (Database& d, const std::string& n, sqlite3_stmt* s)
    : db(&d), name(n), stmt(s)
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

  Result (const Result&) = delete;
  Result& operator= (const Result&) = delete;

  Result (Result&&) = default;
  Result& operator= (Result&&) = default;

  /**
   * Tries to step to the next result.  Returns false if there is none.
   */
  bool Step ();

  /**
   * Extracts the column with the given name as type T.
   */
  template <typename T>
    T Get (const std::string& name) const;

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

  /**
   * Returns the associated name for the query that built this result.
   */
  const std::string&
  GetName () const
  {
    return name;
  }

};

} // namespace pxd

#endif // DATABASE_DATABASE_HPP
