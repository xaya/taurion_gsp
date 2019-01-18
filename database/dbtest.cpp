#include "dbtest.hpp"

#include "schema.hpp"

#include <glog/logging.h>

#include <map>
#include <string>

namespace pxd
{
namespace
{

/**
 * Error callback for SQLite, which prints logs using glog.
 */
void
SQLiteErrorLogger (void* arg, const int errCode, const char* msg)
{
  LOG (ERROR) << "SQLite error (code " << errCode << "): " << msg;
}

/**
 * Database instance that uses a given sqlite3 handle but does its own
 * prepared-statement caching and ID generation.  That way, we can run
 * tests independent of SQLiteGame.
 */
class TestDatabase : public Database
{

private:

  /** Underlying SQLite handle.  */
  sqlite3* const handle = nullptr;

  /** List of cached prepared statements.  */
  std::map<std::string, sqlite3_stmt*> stmtCache;

  /** The next ID to give out.  */
  IdT nextId = 1;

  friend class pxd::DBTestFixture;

protected:

  sqlite3_stmt*
  PrepareStatement (const std::string& sql) override
  {
    CHECK (handle != nullptr);

    const auto mit = stmtCache.find (sql);
    if (mit != stmtCache.end ())
      {
        sqlite3_reset (mit->second);
        CHECK_EQ (sqlite3_clear_bindings (mit->second), SQLITE_OK);
        return mit->second;
      }

    sqlite3_stmt* res = nullptr;
    CHECK_EQ (sqlite3_prepare_v2 (handle, sql.c_str (), sql.size () + 1,
                                  &res, nullptr),
              SQLITE_OK);

    stmtCache.emplace (sql, res);
    return res;
  }

public:

  explicit TestDatabase (sqlite3* h)
    : handle(h)
  {}

  ~TestDatabase ()
  {
    for (const auto& stmt : stmtCache)
      sqlite3_finalize (stmt.second);
    stmtCache.clear ();
  }

  TestDatabase (const TestDatabase&) = delete;
  void operator= (const TestDatabase&) = delete;

  IdT
  GetNextId () override
  {
    return nextId++;
  }

};

} // anonymous namespace

DBTestFixture::DBTestFixture ()
{
  const int rc
      = sqlite3_config (SQLITE_CONFIG_LOG, &SQLiteErrorLogger, nullptr);
  if (rc != SQLITE_OK)
    LOG (WARNING) << "Failed to set up SQLite error handler: " << rc;
  else
    LOG (INFO) << "Configured SQLite error handler";

  LOG (INFO) << "Opening in-memory SQLite database...";
  CHECK_EQ (sqlite3_open (":memory:", &handle), SQLITE_OK);

  db = std::make_unique<TestDatabase> (handle);
}

DBTestFixture::~DBTestFixture ()
{
  /* Make sure to reset the Database instance (including cleaning up the
     prepared statements) before closing the database handle!  */
  db.reset ();

  LOG (INFO) << "Closing underlying SQLite database...";
  sqlite3_close (handle);
}

void
DBTestFixture::SetNextId (const Database::IdT id)
{
  auto* testDb = dynamic_cast<TestDatabase*> (db.get ());
  CHECK (testDb != nullptr);
  testDb->nextId = id;
}

DBTestWithSchema::DBTestWithSchema ()
{
  LOG (INFO) << "Setting up game-state schema in test database...";
  SetupDatabaseSchema (handle);
}

} // namespace pxd
