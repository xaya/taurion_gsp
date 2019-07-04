/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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
