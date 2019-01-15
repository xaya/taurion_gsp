#ifndef DATABASE_DBTEST_HPP
#define DATABASE_DBTEST_HPP

#include "database.hpp"

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <memory>

namespace pxd
{

/**
 * Test fixture that opens an in-memory SQLite3 database and provides the
 * underlying handle to the test.
 */
class DBTestFixture : public testing::Test
{

protected:

  /** The SQLite3 database handle, managed by the test.  */
  sqlite3* handle = nullptr;

  /** Database instance that can be used for tests (based also on handle).  */
  std::unique_ptr<Database> db;

  DBTestFixture ();
  ~DBTestFixture ();

  /**
   * Sets the next auto-ID returned by db->GetNextId.  This is useful for tests
   * when we want to force certain ID ranges.
   */
  void SetNextId (unsigned id);

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
