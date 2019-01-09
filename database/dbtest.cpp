#include "dbtest.hpp"

#include <glog/logging.h>

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
}

DBTestFixture::~DBTestFixture ()
{
  LOG (INFO) << "Closing underlying SQLite database...";
  sqlite3_close (handle);
}

} // namespace pxd
