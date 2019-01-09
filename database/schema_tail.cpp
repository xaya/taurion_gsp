)";

/**
 * Callback for sqlite3_exec that expects not to be called.
 */
int
ExpectNoResult (void* data, int columns, char** strs, char** names)
{
  LOG (FATAL) << "Expected no result from DB query";
}

} // anonymous namespace

void
SetupDatabaseSchema (sqlite3* db)
{
  CHECK_EQ (sqlite3_exec (db, SCHEMA_SQL, &ExpectNoResult, nullptr, nullptr),
            SQLITE_OK);
}

} // namespace pxd
