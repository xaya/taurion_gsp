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

#ifndef DATABASE_COMBAT_HPP
#define DATABASE_COMBAT_HPP

#include "database.hpp"
#include "faction.hpp"
#include "lazyproto.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

namespace pxd
{

/**
 * Database result type for rows that have basic combat fields (i.e.
 * characters and buildings).
 */
struct ResultWithCombat : public Database::ResultType
{
  RESULT_COLUMN (pxd::proto::HP, hp, 53);
  RESULT_COLUMN (pxd::proto::RegenData, regendata, 54);
  RESULT_COLUMN (pxd::proto::TargetId, target, 55);
  RESULT_COLUMN (int64_t, attackrange, 56);
  RESULT_COLUMN (bool, canregen, 57);
};

/**
 * Basic database wrapper type with combat data.  This is a shared superclass
 * between Character and Building.
 */
class CombatEntity
{

protected:

  /** Database reference this belongs to.  */
  Database& db;

  /**
   * Set to true if this is a new entity, so we know that we have to
   * insert it into the database.
   */
  bool isNew;

private:

  /** Current HP proto.  */
  LazyProto<proto::HP> hp;

  /**
   * Data about HP regeneration.  This is accessed often but not updated
   * frequently.  If modified, then we do a full update as per the proto
   * update.  But parsing it should be cheap.
   */
  LazyProto<proto::RegenData> regenData;

  /**
   * The selected target as TargetId proto, if any.  If there is no target,
   * then the underlying database column is NULL, and this proto will have
   * no fields set.
   */
  LazyProto<proto::TargetId> target;

  /**
   * The longest attack or zero if there are none.  This field is loaded
   * from the database but never updated.
   */
  HexCoord::IntT oldAttackRange;

  /**
   * Stores the canregen flag from the database.  We only update it if
   * the RegenData or HP have been modified.
   */
  bool oldCanRegen;

  /**
   * Computes (from HP and RegenData protos) whether or not an entity
   * needs to regenerate HP.
   */
  static bool ComputeCanRegen (const proto::HP& hp,
                               const proto::RegenData& regen);

  /**
   * Computes the attack range of a fighter with the given combat data.
   * Returns zero if there are no attacks at all.
   */
  static HexCoord::IntT FindAttackRange (const proto::CombatData& cd);

  friend class ComputeCanRegenTests;
  friend class FindAttackRangeTests;

protected:

  /**
   * Constructs a new instance meant to be inserted into the DB.
   */
  explicit CombatEntity (Database& d);

  /**
   * Constructs a new instance based on a database result.
   */
  template <typename T>
    explicit CombatEntity (Database& d, const Database::Result<T>& res);

  /**
   * Returns whether a full update of the database (including all the
   * fields) is necessary.
   */
  bool
  IsDirtyFull () const
  {
    return regenData.IsDirty () || target.IsDirty ();
  }

  /**
   * Returns whether a partial update (of the small / fast changing fields
   * like HP) is required.
   */
  bool
  IsDirtyFields () const
  {
    return hp.IsDirty ();
  }

  /**
   * Binds statement parameters for the large / expensive proto fields.
   * Does not include the ones from BindField!
   */
  void
  BindFullFields (Database::Statement& stmt, unsigned indRegenData,
                  unsigned indTarget, unsigned indAttackRange) const;

  /**
   * Binds statement parameters for updating the small / fast changing
   * fields (HP, canRegen).
   */
  void
  BindFields (Database::Statement& stmt, unsigned indHp,
              unsigned indCanRegen) const;

  /**
   * Validates the character state for consistency.  CHECK-fails if there
   * is any mismatch in the fields.
   */
  virtual void Validate () const;

  /**
   * Subclasses must implement this to return whether or not the main proto
   * (with combat data) is dirty.
   */
  virtual bool IsDirtyCombatData () const = 0;

public:

  /**
   * The destructor here does nothing.  It is the subclasses' job to
   * update the database row, using the IsDirty and BindField functions.
   */
  virtual ~CombatEntity () = default;

  const proto::HP&
  GetHP () const
  {
    return hp.Get ();
  }

  proto::HP&
  MutableHP ()
  {
    return hp.Mutable ();
  }

  const proto::RegenData&
  GetRegenData () const
  {
    return regenData.Get ();
  }

  proto::RegenData&
  MutableRegenData ()
  {
    return regenData.Mutable ();
  }

  const proto::TargetId&
  GetTarget () const
  {
    return target.Get ();
  }

  proto::TargetId&
  MutableTarget ()
  {
    return target.Mutable ();
  }

  /**
   * Returns the entity's attack range or zero if there are no attacks.
   * Note that this method must only be called if the instance has been
   * read from the database (not newly constructed) and if its main proto
   * has not been modified.  That allows us to use the cached attack-range
   * column in the database directly.
   */
  HexCoord::IntT GetAttackRange () const;

  /**
   * Returns this entity's target ID as a proto.
   */
  virtual proto::TargetId GetIdAsTarget () const = 0;

  /**
   * Returns the entity's faction (which is needed to determine friendliness).
   */
  virtual Faction GetFaction () const = 0;

  /**
   * Returns the position of this entity for attack targeting.
   */
  virtual const HexCoord& GetCombatPosition () const = 0;

  /**
   * Returns the CombatData proto for this entity, which is likely somewhere
   * extracted from the (type-specific) main proto.
   */
  virtual const proto::CombatData& GetCombatData () const = 0;

};

} // namespace pxd

#include "combat.tpp"

#endif // DATABASE_COMBAT_HPP
