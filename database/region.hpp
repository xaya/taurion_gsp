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

#ifndef DATABASE_REGION_HPP
#define DATABASE_REGION_HPP

#include "database.hpp"
#include "lazyproto.hpp"

#include "mapdata/regionmap.hpp"
#include "proto/region.pb.h"

namespace pxd
{

/**
 * Database result for a row from the regions table.
 */
struct RegionResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (pxd::proto::RegionData, proto, 2);
};

/**
 * Wrapper class around the state of one region in the database.  This
 * abstracts the database accesses themselves away from the other code.
 *
 * Instantiations of this class should be made through the RegionsTable.
 */
class Region
{

private:

  /** Database reference this belongs to.  */
  Database& db;

  /** The ID of the region.  */
  RegionMap::IdT id;

  /** Generic data stored in the proto BLOB.  */
  LazyProto<proto::RegionData> data;

  /**
   * Constructs an instance with "default / empty" data for the given ID.
   */
  explicit Region (Database& d, RegionMap::IdT);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by a RegionsTable.
   */
  explicit Region (Database& d, const Database::Result<RegionResult>& res);

  friend class RegionsTable;

public:

  /**
   * In the destructor, the underlying database is updated if there are any
   * modifications to send.
   */
  ~Region ();

  Region () = delete;
  Region (const Region&) = delete;
  void operator= (const Region&) = delete;

  /* Accessor methods.  */

  RegionMap::IdT
  GetId () const
  {
    return id;
  }

  const proto::RegionData&
  GetProto () const
  {
    return data.Get ();
  }

  proto::RegionData&
  MutableProto ()
  {
    return data.Mutable ();
  }

};

/**
 * Utility class that handles querying the regions table in the database and
 * should be used to obtain Region instances.
 */
class RegionsTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to a region instance.  */
  using Handle = std::unique_ptr<Region>;

  explicit RegionsTable (Database& d)
    : db(d)
  {}

  RegionsTable () = delete;
  RegionsTable (const RegionsTable&) = delete;
  void operator= (const RegionsTable&) = delete;

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<RegionResult>& res);

  /**
   * Returns the region with the given ID.
   */
  Handle GetById (RegionMap::IdT id);

  /**
   * Queries the database for all regions with (potentially) non-empty
   * data stored.  Returns a result set that can be used together with
   * GetFromResult.
   */
  Database::Result<RegionResult> QueryNonTrivial ();

};

} // namespace pxd

#endif // DATABASE_REGION_HPP
