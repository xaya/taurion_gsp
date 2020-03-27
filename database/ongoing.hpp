/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#ifndef DATABASE_ONGOING_HPP
#define DATABASE_ONGOING_HPP

#include "database.hpp"
#include "lazyproto.hpp"

#include "proto/ongoing.pb.h"

#include <memory>
#include <string>

namespace pxd
{

/**
 * Database result type for rows from the ongoing-operations table.
 */
struct OngoingResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (int64_t, height, 2);
  RESULT_COLUMN (int64_t, character, 3);
  RESULT_COLUMN (int64_t, building, 4);
  RESULT_COLUMN (pxd::proto::OngoingOperation, proto, 5);
};

/**
 * Wrapper class around an ongoing operation in the database.  Instances
 * should be obtained through OngoingTable.
 */
class OngoingOperation
{

private:

  /** Database reference this belongs to.  */
  Database& db;

  /** The underlying ID in the database.  */
  Database::IdT id;

  /** The block height at which it needs to be processed.  */
  unsigned height;

  /** The associated character ID (or EMPTY_ID if none).  */
  Database::IdT characterId;

  /** The associated building ID (or EMPTY_ID if none).  */
  Database::IdT buildingId;

  /** General proto data.  */
  LazyProto<proto::OngoingOperation> data;

  /** Whether or not the fields are dirty.  */
  bool dirtyFields;

  /**
   * Constructs a new instance with auto-generated ID meant to be inserted
   * into the database.
   */
  explicit OngoingOperation (Database& d);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by an OngoingsTable.
   */
  explicit OngoingOperation (Database& d,
                             const Database::Result<OngoingResult>& res);

  friend class OngoingsTable;

public:

  /**
   * In the destructor, the underlying database is updated if there are any
   * modifications to send.
   */
  ~OngoingOperation ();

  OngoingOperation () = delete;
  OngoingOperation (const OngoingOperation&) = delete;
  void operator= (const OngoingOperation&) = delete;

  Database::IdT
  GetId () const
  {
    return id;
  }

  unsigned
  GetHeight () const
  {
    return height;
  }

  void
  SetHeight (const unsigned h)
  {
    height = h;
    dirtyFields = true;
  }

  Database::IdT
  GetCharacterId () const
  {
    return characterId;
  }

  void
  SetCharacterId (const Database::IdT id)
  {
    characterId = id;
    dirtyFields = true;
  }

  Database::IdT
  GetBuildingId () const
  {
    return buildingId;
  }

  void
  SetBuildingId (const Database::IdT id)
  {
    buildingId = id;
    dirtyFields = true;
  }

  const proto::OngoingOperation&
  GetProto () const
  {
    return data.Get ();
  }

  proto::OngoingOperation&
  MutableProto ()
  {
    return data.Mutable ();
  }

};

/**
 * Utility class that handles querying the ongoings table in the database and
 * should be used to obtain OngoingOperation instances.
 */
class OngoingsTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to an instance.  */
  using Handle = std::unique_ptr<OngoingOperation>;

  explicit OngoingsTable (Database& d)
    : db(d)
  {}

  OngoingsTable () = delete;
  OngoingsTable (const OngoingsTable&) = delete;
  void operator= (const OngoingsTable&) = delete;

  /**
   * Creates a new entry in the database and returns the handle so it
   * can be initialised.
   */
  Handle CreateNew ();

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<OngoingResult>& res);

  /**
   * Returns a handle for the given ID (or null if it doesn't exist).
   */
  Handle GetById (Database::IdT id);

  /**
   * Queries the database for all ongoing operations.
   */
  Database::Result<OngoingResult> QueryAll ();

  /**
   * Queries the database for all operations that need processing at the
   * given (current) block height.
   */
  Database::Result<OngoingResult> QueryForHeight (unsigned h);

  /**
   * Deletes all operations for a given character ID.  This is used when
   * the character dies.
   */
  void DeleteForCharacter (Database::IdT id);

  /**
   * Deletes all operations for a given building ID.  This is used when
   * the building is destroyed.
   */
  void DeleteForBuilding (Database::IdT id);

  /**
   * Deletes all operations with the given height.  This is used to clean
   * up finished operations after processing them.
   */
  void DeleteForHeight (unsigned h);

};

} // namespace pxd

#endif // DATABASE_ONGOING_HPP
