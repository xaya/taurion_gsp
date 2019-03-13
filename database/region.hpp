#ifndef DATABASE_REGION_HPP
#define DATABASE_REGION_HPP

#include "database.hpp"

#include "mapdata/regionmap.hpp"
#include "proto/region.pb.h"

namespace pxd
{

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
  proto::RegionData data;

  /**
   * Set to true if any modification has been made and we need to write
   * the changes back to the database in the destructor.
   */
  bool dirty;

  /**
   * Constructs an instance with "default / empty" data for the given ID.
   */
  explicit Region (Database& d, RegionMap::IdT);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by a RegionsTable.
   */
  explicit Region (Database& d, const Database::Result& res);

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
    return data;
  }

  proto::RegionData&
  MutableProto ()
  {
    dirty = true;
    return data;
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
  Handle GetFromResult (const Database::Result& res);

  /**
   * Returns the region with the given ID.
   */
  Handle GetById (RegionMap::IdT id);

  /**
   * Queries the database for all regions with (potentially) non-empty
   * data stored.  Returns a result set that can be used together with
   * GetFromResult.
   */
  Database::Result QueryNonTrivial ();

};

} // namespace pxd

#endif // DATABASE_REGION_HPP
