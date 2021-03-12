/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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
#include "inventory.hpp"
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
  RESULT_COLUMN (int64_t, modifiedheight, 2);
  RESULT_COLUMN (int64_t, resourceleft, 3);
  RESULT_COLUMN (pxd::proto::RegionData, proto, 4);
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

  /**
   * Current block height.  When the region is actually modified, we use this
   * to set the last modified block height in the database.
   */
  unsigned currentHeight;

  /** The ID of the region.  */
  RegionMap::IdT id;

  /** The UniqueHandles tracker for this instance.  */
  Database::HandleTracker tracker;

  /** The amount of mine-able resources left.  */
  Quantity resourceLeft;

  /** Generic data stored in the proto BLOB.  */
  LazyProto<proto::RegionData> data;

  /** Whether or not just the non-proto fields have been updated.  */
  bool dirtyFields;

  /**
   * Constructs an instance with "default / empty" data for the given ID.
   */
  explicit Region (Database& d, unsigned h, RegionMap::IdT);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by a RegionsTable.
   */
  explicit Region (Database& d, unsigned h,
                   const Database::Result<RegionResult>& res);

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

  /**
   * Returns the amount of mine-able resource left in this region.  This must
   * only be called when the region has been prospected already.  The type of
   * resource can be found in the proto data.
   */
  Quantity GetResourceLeft () const;

  /**
   * Sets the amount of mine-able resource left.  This must only be called
   * when the region has been prospected.
   */
  void SetResourceLeft (Quantity value);

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

  /**
   * Current block height.  This is used to set the "last changed height"
   * for modified regions.
   */
  unsigned height;

  /**
   * Sets the height to a different value.  We need this for some tests so
   * that we can reuse an existing RegionsTable instance for processing
   * multiple blocks.
   */
  void
  SetHeightForTesting (const unsigned h)
  {
    height = h;
  }

  friend class PXLogicTests;

public:

  /**
   * Block height to pass if we just want a read-only view of regions and
   * are not processing a block at the moment.
   */
  static constexpr unsigned HEIGHT_READONLY = 0;

  /** Movable handle to a region instance.  */
  using Handle = std::unique_ptr<Region>;

  /**
   * Constructs the table.  In order to make modifications, the current block
   * height must be set.  If only data needs to be read, then it is possible
   * to set the height to HEIGHT_READONLY.
   */
  explicit RegionsTable (Database& d, const unsigned h)
    : db(d), height(h)
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

  /**
   * Queries the database for all regions that were modified later (or equal)
   * to the given block height.
   */
  Database::Result<RegionResult> QueryModifiedSince (unsigned h);

};

} // namespace pxd

#endif // DATABASE_REGION_HPP
