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

#ifndef DATABASE_FIGHTER_HPP
#define DATABASE_FIGHTER_HPP

#include "building.hpp"
#include "character.hpp"
#include "combat.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <functional>
#include <memory>

namespace pxd
{

/**
 * Database interface for retrieving handles to all fighters (i.e. entities
 * that need to be processed for combat).
 */
class FighterTable
{

private:

  /** The BuildingsTable from which we retrieve building-based fighters.  */
  BuildingsTable& buildings;

  /** The CharacterTable from which we retrieve character-based fighters.  */
  CharacterTable& characters;

public:

  /** Handle to a generic fighter entity.  */
  using Handle = std::unique_ptr<CombatEntity>;

  /** Type for callbacks when querying for all fighters.  */
  using Callback = std::function<void (Handle f)>;

  /**
   * Constructs a fighter table drawing buildings and characters from the given
   * database table wrappers.
   */
  explicit FighterTable (BuildingsTable& b, CharacterTable& c)
    : buildings(b), characters(c)
  {}

  FighterTable () = delete;
  FighterTable (const FighterTable&) = delete;
  void operator= (const FighterTable&) = delete;

  /**
   * Retrieves the fighter handle for the given target ID.
   */
  Handle GetForTarget (const proto::TargetId& id);

  /**
   * Retrieves all fighters from the database that have an attack and runs
   * the callback on each one.  This includes fighters with only friendly
   * attacks, and hence essentially means "process everyone that needs it
   * for target finding".
   */
  void ProcessWithAttacks (const Callback& cb);

  /**
   * Retrieves and processes all fighters that need HP regeneration.
   */
  void ProcessForRegen (const Callback& cb);

  /**
   * Retrieves and processes all fighers that have a target, i.e. for whom
   * we need to deal damage.  This includes fighters that have only
   * friendlies in range but a friendly attack.
   */
  void ProcessWithTarget (const Callback& cb);

  /**
   * Removes all combat effects in the database.
   */
  void ClearAllEffects ();

};

} // namespace pxd

#endif // DATABASE_FIGHTER_HPP
