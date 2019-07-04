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

#ifndef DATABASE_FIGHTER_HPP
#define DATABASE_FIGHTER_HPP

#include "character.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <functional>

namespace pxd
{

/**
 * Database abstraction for a fighter (entity that can attack another or take
 * damage from another one's attack).  This can be either a character or a
 * building.  This class makes both look the same for the processing code.
 */
class Fighter
{

private:

  /** The character handle if this is a character.  */
  CharacterTable::Handle character;

  /**
   * Construct a fighter based on a character handle.
   */
  explicit Fighter (CharacterTable::Handle c)
    : character(std::move (c))
  {}

  friend class FighterTable;

public:

  Fighter () = default;
  Fighter (Fighter&&) = default;
  Fighter& operator= (Fighter&&) = default;

  Fighter (const Fighter&) = delete;
  void operator= (const Fighter&) = delete;

  /**
   * Returns this fighter's type and ID.
   */
  proto::TargetId GetId () const;

  /**
   * Returns this fighter's faction association.  This is used to determine
   * friendlyness towards potential targets.
   */
  Faction GetFaction () const;

  /**
   * Returns the figher's position.  Must not be called if this is an
   * empty handle.
   */
  const HexCoord& GetPosition () const;

  /**
   * Returns the combat data proto for this fighter.
   */
  const proto::CombatData& GetCombatData () const;

  /**
   * Returns the target.  This must only be called if there is one.
   */
  const proto::TargetId& GetTarget () const;

  /**
   * Sets the target of this fighter to the given proto.
   */
  void SetTarget (const proto::TargetId& target);

  /**
   * Clears target selection (i.e. no target is selected).
   */
  void ClearTarget ();

  /**
   * Returns a read-only reference to the current HP.
   */
  const proto::HP& GetHP () const;

  /**
   * Returns a mutable reference to the current HP so that they can be modified
   * (for dealing damage and for regenerating the shield).
   */
  proto::HP& MutableHP ();

  /**
   * Resets the handle to be empty.
   */
  void reset ();

  /**
   * Checks whether or not this is an "empty" pointer.
   */
  bool empty () const;

};

/**
 * Database interface for retrieving handles to all fighters.
 */
class FighterTable
{

private:

  /** The CharacterTable from which we retrieve character-based fighters.  */
  CharacterTable& characters;

public:

  /** Type for callbacks when querying for all fighters.  */
  using Callback = std::function<void (Fighter f)>;

  /**
   * Constructs a fighter table drawing characters from the given table.
   */
  explicit FighterTable (CharacterTable& c)
    : characters(c)
  {}

  FighterTable () = delete;
  FighterTable (const FighterTable&) = delete;
  void operator= (const FighterTable&) = delete;

  /**
   * Retrieves the fighter handle for the given target ID.
   */
  Fighter GetForTarget (const proto::TargetId& id);

  /**
   * Retrieves all fighters from the database and runs the callback
   * on each one.
   */
  void ProcessAll (const Callback& cb);

  /**
   * Retrieves and processes all fighers that have a target, i.e. for whom
   * we need to deal damage.
   */
  void ProcessWithTarget (const Callback& cb);

};

} // namespace pxd

#endif // DATABASE_FIGHTER_HPP
