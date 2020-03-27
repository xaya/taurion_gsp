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

#ifndef DATABASE_CHARACTER_HPP
#define DATABASE_CHARACTER_HPP

#include "combat.hpp"
#include "coord.hpp"
#include "database.hpp"
#include "faction.hpp"
#include "inventory.hpp"
#include "lazyproto.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"
#include "proto/character.pb.h"
#include "proto/combat.pb.h"
#include "proto/movement.pb.h"

#include <functional>
#include <memory>
#include <string>

namespace pxd
{

/**
 * Database result type for rows from the characters table.
 */
struct CharacterResult : public ResultWithFaction, public ResultWithCoord,
                         public ResultWithCombat
{
  RESULT_COLUMN (int64_t, id, 1);
  RESULT_COLUMN (std::string, owner, 2);
  RESULT_COLUMN (int64_t, inbuilding, 3);
  RESULT_COLUMN (int64_t, enterbuilding, 4);
  RESULT_COLUMN (pxd::proto::VolatileMovement, volatilemv, 5);
  RESULT_COLUMN (pxd::proto::Inventory, inventory, 6);
  RESULT_COLUMN (pxd::proto::Character, proto, 7);
};

/**
 * Wrapper class for the state of one character.  This connects the actual game
 * logic (reading the state and doing modifications to it) from the database.
 * All interpretation of database results and upates to the database are done
 * through this class.
 *
 * This class should not be instantiated directly by users.  Instead, the
 * methods from CharacterTable should be used.  Furthermore, variables should
 * be of type CharacterTable::Handle (or using auto) to get move semantics.
 */
class Character : public CombatEntity
{

private:

  /** The underlying integer ID in the database.  */
  Database::IdT id;

  /** The owner string.  */
  std::string owner;

  /** The character's faction.  This is immutable.  */
  Faction faction;

  /** The current position.  */
  HexCoord pos;

  /** The building the character is in, or EMPTY_ID if outside.  */
  Database::IdT inBuilding;

  /** The building the character wants to enter or EMPTY_ID.  */
  Database::IdT enterBuilding;

  /** Volatile movement proto.  */
  LazyProto<proto::VolatileMovement> volatileMv;

  /** The character's inventory.  */
  Inventory inv;

  /** All other data in the protocol buffer.  */
  LazyProto<proto::Character> data;

  /**
   * Set to true if any modification to the non-proto columns was made that
   * needs to be synced back to the database in the destructor.
   */
  bool dirtyFields;

  /**
   * Constructs a new character with an auto-generated ID meant to be inserted
   * into the database.
   */
  explicit Character (Database& d, const std::string& o, Faction f);

  /**
   * Constructs a character instance based on the given query result.  This
   * represents the data from the result row but can then be modified.  The
   * result should come from a query made through CharacterTable.
   */
  explicit Character (Database& d,
                      const Database::Result<CharacterResult>& res);

  /**
   * Binds parameters in a statement to the mutable non-proto fields.  This is
   * to share code between the proto and non-proto updates.  The ID is always
   * bound to parameter ?1, and other fields to following integer IDs.
   *
   * The immutable non-proto field faction is also not bound
   * here, since it is only present in the INSERT OR REPLACE statement
   * (with proto update) and not the UPDATE one.
   */
  void BindFieldValues (Database::Statement& stmt) const;

  friend class CharacterTable;

protected:

  void Validate () const override;

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
  ~Character ();

  Character () = delete;
  Character (const Character&) = delete;
  void operator= (const Character&) = delete;

  /* Accessor methods.  */

  Database::IdT
  GetId () const
  {
    return id;
  }

  const std::string&
  GetOwner () const
  {
    return owner;
  }

  void
  SetOwner (const std::string& o)
  {
    dirtyFields = true;
    owner = o;
  }

  Faction
  GetFaction () const override
  {
    return faction;
  }

  bool
  IsInBuilding () const
  {
    return inBuilding != Database::EMPTY_ID;
  }

  /**
   * Returns the on-map position.  Must not be called if in building.
   */
  const HexCoord& GetPosition () const;

  void
  SetPosition (const HexCoord& c)
  {
    dirtyFields = true;
    inBuilding = Database::EMPTY_ID;
    pos = c;
  }

  /**
   * Returns the building ID the character is in.  Must only be called if
   * it actually is in a building.
   */
  Database::IdT GetBuildingId () const;

  void
  SetBuildingId (const Database::IdT id)
  {
    dirtyFields = true;
    inBuilding = id;
  }

  Database::IdT
  GetEnterBuilding () const
  {
    return enterBuilding;
  }

  void
  SetEnterBuilding (const Database::IdT id)
  {
    dirtyFields = true;
    enterBuilding = id;
  }

  const proto::VolatileMovement&
  GetVolatileMv () const
  {
    return volatileMv.Get ();
  }

  proto::VolatileMovement&
  MutableVolatileMv ()
  {
    return volatileMv.Mutable ();
  }

  /**
   * Returns true if the character is currently busy, i.e. has an ongoing
   * operation in its proto.
   */
  bool IsBusy () const;

  const Inventory&
  GetInventory () const
  {
    return inv;
  }

  Inventory&
  GetInventory ()
  {
    return inv;
  }

  const proto::Character&
  GetProto () const
  {
    return data.Get ();
  }

  proto::Character&
  MutableProto ()
  {
    return data.Mutable ();
  }

  /**
   * Returns the used cargo space for the character's inventory.
   */
  uint64_t UsedCargoSpace () const;

  proto::TargetId GetIdAsTarget () const override;

  const HexCoord&
  GetCombatPosition () const override
  {
    return GetPosition ();
  }

  const proto::CombatData&
  GetCombatData () const override
  {
    return data.Get ().combat_data ();
  }

};

/**
 * Utility class that handles querying the characters table in the database and
 * should be used to obtain Character instances (or rather, the underlying
 * Database::Result's for them).
 */
class CharacterTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to a character instance.  */
  using Handle = std::unique_ptr<Character>;

  /** Callback function for processing positions and factions of characters.  */
  using PositionFcn
      = std::function<void (Database::IdT id, const HexCoord& pos, Faction f)>;

  explicit CharacterTable (Database& d)
    : db(d)
  {}

  CharacterTable () = delete;
  CharacterTable (const CharacterTable&) = delete;
  void operator= (const CharacterTable&) = delete;

  /**
   * Returns a Character handle for a fresh instance corresponding to a new
   * character that will be created.
   */
  Handle CreateNew (const std::string& owner, Faction faction);

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result<CharacterResult>& res);

  /**
   * Returns the character with the given ID or a null handle if there is
   * none with that ID.
   */
  Handle GetById (Database::IdT id);

  /**
   * Queries for all characters in the database table.  The characters are
   * ordered by ID to make the result deterministic.
   */
  Database::Result<CharacterResult> QueryAll ();

  /**
   * Queries for all characters with a given owner, ordered by ID.
   */
  Database::Result<CharacterResult> QueryForOwner (const std::string& owner);

  /**
   * Queries all characters that are in a given building.
   */
  Database::Result<CharacterResult> QueryForBuilding (Database::IdT building);

  /**
   * Queries for all characters that are currently moving (and thus may need
   * to be updated for move stepping).
   */
  Database::Result<CharacterResult> QueryMoving ();

  /**
   * Queries for all characters that are currently mining.
   */
  Database::Result<CharacterResult> QueryMining ();

  /**
   * Queries for all characters with attacks.  This only includes characters
   * on the map, as characters in buildings can't attack anyway.
   */
  Database::Result<CharacterResult> QueryWithAttacks ();

  /**
   * Queries for all characters that may need to have HP regenerated.
   */
  Database::Result<CharacterResult> QueryForRegen ();

  /**
   * Queries for all characters that have a combat target and thus need
   * to be processed for damage.
   */
  Database::Result<CharacterResult> QueryWithTarget ();

  /**
   * Queries all characters that want to enter a building.
   */
  Database::Result<CharacterResult> QueryForEnterBuilding ();

  /**
   * Processes all positions of characters on the map.  This is used to
   * construct the dynamic obstacle map, avoiding the need to query all data
   * for each character and construct a full Character handle.
   * Characters in buildings are ignored by this function.
   */
  void ProcessAllPositions (const PositionFcn& cb);

  /**
   * Deletes the character with the given ID.
   */
  void DeleteById (Database::IdT id);

  /**
   * Returns the number of characters owned by the given account.
   */
  unsigned CountForOwner (const std::string& owner);

};

} // namespace pxd

#endif // DATABASE_CHARACTER_HPP
