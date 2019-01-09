#ifndef DATABASE_SCHEMA_HPP
#define DATABASE_SCHEMA_HPP

#include <sqlite3.h>

namespace pxd
{

/**
 * Create the database schema (if it does not exist yet) in the given database
 * connection.
 */
void SetupDatabaseSchema (sqlite3* db);

} // namespace pxd

#endif // DATABASE_SCHEMA_HPP
