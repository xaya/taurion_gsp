#ifndef DATABASE_DBTEST_HPP
#define DATABASE_DBTEST_HPP

#include "database.hpp"

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <map>
#include <memory>
#include <string>

namespace pxd
{

/**
 * Database instance that uses an in-memory SQLite and does its own
 * statement caching and ID handling.  That way, we can run tests and
 * benchmarks independently from SQLiteGame.
 */
class TestDatabase : public Database
{

private:

  /** The SQLite database handle.  */
  sqlite3* handle = nullptr;

  /** List of cached prepared statements.  */
  std::map<std::string, sqlite3_stmt*> stmtCache;

  /** The next ID to give out.  */
  IdT nextId = 1;

protected:

  sqlite3_stmt* PrepareStatement (const std::string& sql);

public:

  TestDatabase ();
  ~TestDatabase ();

  TestDatabase (const TestDatabase&) = delete;
  void operator= (const TestDatabase&) = delete;

  IdT GetNextId () override;

  /**
   * Sets the next ID to be given out.  This is useful for tests to force
   * certain ID ranges.
   */
  void
  SetNextId (const IdT id)
  {
    nextId = id;
  }

  /**
   * Returns the underlying database handle for SQLite.
   */
  sqlite3*
  GetHandle ()
  {
    return handle;
  }

};

/**
 * Test fixture that has a TestDatabase inside.
 */
class DBTestFixture : public testing::Test
{

protected:

  /** The database instance to use.  */
  TestDatabase db;

  DBTestFixture () = default;

};

/**
 * Test fixture that opens an in-memory database and also installs the
 * game-state schema in it.
 */
class DBTestWithSchema : public DBTestFixture
{

protected:

  DBTestWithSchema ();

};

} // namespace pxd

#endif // DATABASE_DBTEST_HPP
