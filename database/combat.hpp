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
  RESULT_COLUMN (bool, friendlytargets, 56);
  RESULT_COLUMN (int64_t, attackrange, 57);
  RESULT_COLUMN (int64_t, friendlyrange, 58);
  RESULT_COLUMN (bool, canregen, 59);
  RESULT_COLUMN (pxd::proto::OtherTargets, othertargets, 60);
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
   * The targets of attacks with a shorter range, where they are not the
   * main target.  The underlying column is NULL if there are none.
   */
  LazyProto<proto::OtherTargets> otherTargets;

  /**
   * Whether or not friendly targets are in range.  This is set accordingly
   * by the target finding phase, and will be used to determine whether or
   * not we need to process them.
   */
  bool friendlyTargets;

  /**
   * The longest attack or NO_ATTACKS if there are none.  This field is loaded
   * from the database but never updated.
   */
  HexCoord::IntT oldAttackRange;

  /**
   * The longest friendly attack range or NO_ATTACKS if there is none.  This is
   * loaded from the database but never updated.
   */
  HexCoord::IntT oldFriendlyRange;

  /**
   * Stores the canregen flag from the database.  We only update it if
   * the RegenData or HP have been modified.
   */
  bool oldCanRegen;

  /** Set to true if non-proto fields have been changed.  */
  bool isDirty;

  /**
   * Computes (from HP and RegenData protos) whether or not an entity
   * needs to regenerate HP.
   */
  static bool ComputeCanRegen (const proto::HP& hp,
                               const proto::RegenData& regen);

  /**
   * Computes the attack range of a fighter with the given combat data,
   * or the range of the longest friendly attack.
   *
   * Returns NO_ATTACKS if there are no attacks at all.
   */
  static HexCoord::IntT FindAttackRange (const proto::CombatData& cd,
                                         bool friendly);

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
    return regenData.IsDirty ();
  }

  /**
   * Returns whether a partial update (of the small / fast changing fields
   * like HP and the target) is required.
   */
  bool
  IsDirtyFields () const
  {
    return isDirty || hp.IsDirty () || target.IsDirty ()
              || otherTargets.IsDirty ();
  }

  /**
   * Sets the columns of the large / expensive proto fields in a row
   * being written.  Does not include the ones from SetFields!
   */
  void SetFullFields (Database::RowWriter& row) const;

  /**
   * Sets the columns of the small / fast changing fields (HP, canRegen,
   * friendly targets, target) in a row being written.  If all is false,
   * only the fields that changed are set.
   */
  void SetFields (Database::RowWriter& row, bool all) const;

  /**
   * Validates the state for consistency.  CHECK-fails if there
   * is any mismatch in the fields.
   */
  virtual void Validate () const;

  /**
   * Subclasses must implement this to return whether or not the main proto
   * (with combat data) is dirty.
   */
  virtual bool IsDirtyCombatData () const = 0;

public:

  /** Magic value for attack range if there are no attacks.  */
  static constexpr HexCoord::IntT NO_ATTACKS = -1;

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

  bool
  HasTarget () const
  {
    return target.Get ().has_id ();
  }

  const proto::TargetId& GetTarget () const;

  /**
   * Clears the target, and with it the other targets.
   */
  void ClearTarget ();

  void SetTarget (const proto::TargetId& t);

  const proto::OtherTargets&
  GetOtherTargets () const
  {
    return otherTargets.Get ();
  }

  /**
   * Sets the other targets.  They must only be set while there is a target.
   */
  void SetOtherTargets (const proto::OtherTargets& t);

  bool
  HasFriendlyTargets () const
  {
    return friendlyTargets;
  }

  void
  SetFriendlyTargets (const bool val)
  {
    if (friendlyTargets != val)
      isDirty = true;
    friendlyTargets = val;
  }

  /**
   * Returns the entity's attack range or NO_ATTACKS if there are no attacks.
   * Note that this method must only be called if the instance has been
   * read from the database (not newly constructed) and if its main proto
   * has not been modified.  That allows us to use the cached attack-range
   * column in the database directly.
   *
   * The attack range returned is just based on the base stats of the entity.
   * It does not take combat effects, low-HP-boosts or things like that
   * into account.
   *
   * If friendly is true, it returns the longest attack affecting friendlies
   * instead of the normal attack range.
   */
  HexCoord::IntT GetAttackRange (bool friendly) const;

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

  /**
   * Returns the combat effects applied to this entity.
   */
  virtual const proto::CombatEffects& GetEffects () const = 0;

  /**
   * Returns a mutable reference to the effects proto.
   */
  virtual proto::CombatEffects& MutableEffects () = 0;

  /**
   * Returns the canregen flag for the current HP and RegenData, i.e. whether
   * HP regeneration will process this entity once it is written.
   */
  bool GetNewCanRegen () const;

  /**
   * Resets the effects in memory to none after they have been cleared
   * in the database (by ClearAllEffects of the table), so that the
   * handle neither writes the old ones back nor needs another write.
   */
  virtual void EffectsCleared () = 0;

};

} // namespace pxd

#include "combat.tpp"

#endif // DATABASE_COMBAT_HPP
