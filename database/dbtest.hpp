/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#ifndef DATABASE_DBTEST_HPP
#define DATABASE_DBTEST_HPP

#include "database.hpp"

#include <xayagame/sqlitestorage.hpp>

#include <gtest/gtest.h>

#include <benchmark/benchmark.h>

#include <sqlite3.h>

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

  /** The SQLiteDatabase instance.  */
  xaya::SQLiteDatabase db;

  /** The next ID to give out.  */
  IdT nextId = 1;

public:

  TestDatabase ();

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
    return *db;
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

/**
 * RAII object to checkpoint the current database state and restore it
 * when destructed.  It also pauses the benchmark timers.  This can be
 * used in benchmarks to run the loop with the same database state each
 * iteration.
 */
class TemporaryDatabaseChanges
{

private:

  /** Underlying database handle.  */
  Database& db;

  /** Benchmark state for pausing the timers.  */
  benchmark::State& benchmarkState;

public:

  /**
   * Constructs the object.  This checkpoints the database state.
   */
  explicit TemporaryDatabaseChanges (Database& d, benchmark::State& s);

  /**
   * Destructs the object, restoring the checkpointed state.
   */
  ~TemporaryDatabaseChanges ();

  TemporaryDatabaseChanges () = delete;
  TemporaryDatabaseChanges (const TemporaryDatabaseChanges&) = delete;
  void operator= (const TemporaryDatabaseChanges&) = delete;

};

} // namespace pxd

#endif // DATABASE_DBTEST_HPP
