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

#ifndef DATABASE_BUILDING_HPP
#define DATABASE_BUILDING_HPP

#include "combat.hpp"
#include "coord.hpp"
#include "database.hpp"
#include "faction.hpp"
#include "lazyproto.hpp"

#include "proto/building.pb.h"
#include "proto/config.pb.h"

namespace pxd
{

/**
 * Database result for a row from the buildings table.
 */
struct BuildingResult : public ResultWithFaction, public ResultWithCoord,
                        public ResultWithCombat
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (std::string, type, 2);
  RESULT_COLUMN (std::string, owner, 3);
  RESULT_COLUMN (pxd::proto::Building, proto, 4);
};

/**
 * Wrapper class around the state of one building in the database.  This
 * abstracts the database accesses themselves away from the other code.
 *
 * Instantiations of this class should be made through the BuildingsTable.
 */
class Building : public CombatEntity
{

private:

  /** The ID of the building.  */
  Database::IdT id;

  /** The building's type.  This is immutable.  */
  std::string type;

  /** The owner string.  */
  std::string owner;

  /** The owner's faction.  This is immutable.  */
  Faction faction;

  /** The building's centre position.  */
  HexCoord pos;

  /** Generic data stored in the proto BLOB.  */
  LazyProto<proto::Building> data;

  /** Whether or not non-proto fields have been modified.  */
  bool dirtyFields;

  /**
   * Constructs a new instance with auto-generated ID that is meant
   * to be inserted into the database.
   */
  explicit Building (Database& d, const std::string& t,
                     const std::string& o, Faction f);

  /**
   * Constructs an instance based on the given DB result set.  The result
   * set should be constructed by a BuildingsTable.
   */
  explicit Building (Database& d, const Database::Result<BuildingResult>& res);

  friend class BuildingsTable;

protected:

  bool
  IsDirtyCombatData () const override
  {
    return data.IsDirty ();
  }

public:

  /**
   * In the destructor, the underlying database is updated if there are any
   * modifications to send.
   */
  ~Building ();

  Building () = delete;
  Building (const Building&) = delete;
  void operator= (const Building&) = delete;

  /* Accessor methods.  */

  Database::IdT
  GetId () const
  {
    return id;
  }

  const std::string&
  GetType () const
  {
    return type;
  }

  Faction
  GetFaction () const override
  {
    return faction;
  }

  /**
   * Returns the building's owner account.  This must not be called for ancient
   * buildings (which do not have any owner).
   */
  const std::string& GetOwner () const;

  /**
   * Sets the owner account.  Must not be called for ancient buildings.
   */
  void SetOwner (const std::string& o);

  const HexCoord&
  GetCentre () const
  {
    return pos;
  }

  /**
   * Modifies the centre coordinate.  This is only allowed on new buildings.
   */
  void SetCentre (const HexCoord& c);

  const proto::Building&
  GetProto () const
  {
    return data.Get ();
  }

  proto::Building&
  MutableProto ()
  {
    return data.Mutable ();
  }

  proto::TargetId GetIdAsTarget () const override;

  const HexCoord&
  GetCombatPosition () const override
  {
    return GetCentre ();
  }

  const proto::CombatData&
  GetCombatData () const override
  {
    return data.Get ().combat_data ();
  }

  /**
   * Returns the rodata for the building type.
   */
  const proto::BuildingData& RoConfigData () const;

};

/**
 * Utility class that handles querying the buildings table in the database and
 * should be used to obtain Building instances.
 */
class BuildingsTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to a region instance.  */
  using Handle = std::unique_ptr<Building>;

  /**
   * Constructs the table.
   */
  explicit BuildingsTable (Database& d)
    : db(d)
  {}

  BuildingsTable () = delete;
  BuildingsTable (const BuildingsTable&) = delete;
  void operator= (const BuildingsTable&) = delete;

  /**
   * Creates a new building that will be inserted into the database.  If the
   * faction is ANCIENT, then owner must be the empty string.
   */
  Handle CreateNew (const std::string& type,
                    const std::string& owner, const Faction faction);

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<BuildingResult>& res);

  /**
   * Returns the building with the given ID.
   */
  Handle GetById (Database::IdT id);

  /**
   * Queries the database for all buildings.
   */
  Database::Result<BuildingResult> QueryAll ();

  /**
   * Queries for all buildings with attacks.
   */
  Database::Result<BuildingResult> QueryWithAttacks ();

  /**
   * Queries for all buildings that may need to have HP regenerated.
   */
  Database::Result<BuildingResult> QueryForRegen ();

  /**
   * Queries for all buildings that have a combat target and thus need
   * to be processed for damage.
   */
  Database::Result<BuildingResult> QueryWithTarget ();

  /**
   * Deletes the row for a given building ID.
   */
  void DeleteById (Database::IdT id);

};

} // namespace pxd

#endif // DATABASE_BUILDING_HPP
