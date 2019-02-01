#include "dbtest.hpp"

#include "schema.hpp"

#include <glog/logging.h>

namespace pxd
{
namespace
{

/**
 * Whether or not the error handler has already been set up.  This is used
 * to ensure that we do it only once, even with many unit tests or other
 * things running in one binary.
 */
bool errorLoggerSet = false;

/**
 * Error callback for SQLite, which prints logs using glog.
 */
void
SQLiteErrorLogger (void* arg, const int errCode, const char* msg)
{
  LOG (ERROR) << "SQLite error (code " << errCode << "): " << msg;
}

} // anonymous namespace

TestDatabase::TestDatabase ()
{
  if (!errorLoggerSet)
    {
      errorLoggerSet = true;
      const int rc
          = sqlite3_config (SQLITE_CONFIG_LOG, &SQLiteErrorLogger, nullptr);
      if (rc != SQLITE_OK)
        LOG (WARNING) << "Failed to set up SQLite error handler: " << rc;
      else
        LOG (INFO) << "Configured SQLite error handler";
    }

  LOG (INFO) << "Opening in-memory SQLite database...";
  CHECK_EQ (sqlite3_open (":memory:", &handle), SQLITE_OK);
}

TestDatabase::~TestDatabase ()
{
  for (const auto& stmt : stmtCache)
    sqlite3_finalize (stmt.second);
  stmtCache.clear ();

  LOG (INFO) << "Closing underlying SQLite database...";
  sqlite3_close (handle);
}

sqlite3_stmt*
TestDatabase::PrepareStatement (const std::string& sql)
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

Database::IdT
TestDatabase::GetNextId ()
{
  return nextId++;
}

DBTestWithSchema::DBTestWithSchema ()
{
  LOG (INFO) << "Setting up game-state schema in test database...";
  SetupDatabaseSchema (db.GetHandle ());
}

TemporaryDatabaseChanges::TemporaryDatabaseChanges (Database& d,
                                                    benchmark::State& s)
  : db(d), benchmarkState(s)
{
  benchmarkState.PauseTiming ();
  auto stmt = db.Prepare ("SAVEPOINT `TemporaryDatabaseChanges`");
  stmt.Execute ();
  benchmarkState.ResumeTiming ();
}

TemporaryDatabaseChanges::~TemporaryDatabaseChanges ()
{
  benchmarkState.PauseTiming ();
  auto stmt = db.Prepare ("ROLLBACK TO `TemporaryDatabaseChanges`");
  stmt.Execute ();
  benchmarkState.ResumeTiming ();
}

} // namespace pxd
