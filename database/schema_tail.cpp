)";

} // anonymous namespace

void
SetupDatabaseSchema (xaya::SQLiteDatabase& db)
{
  db.Execute (SCHEMA_SQL);
}

} // namespace pxd
