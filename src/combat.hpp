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

#ifndef PXD_COMBAT_HPP
#define PXD_COMBAT_HPP

#include "context.hpp"
#include "fame.hpp"

#include "database/damagelists.hpp"
#include "database/database.hpp"
#include "database/inventory.hpp"
#include "mapdata/basemap.hpp"
#include "proto/combat.pb.h"

#include <xayautil/random.hpp>

#include <set>
#include <utility>

namespace pxd
{

/**
 * Representation of a Target that can be used as key in a map or as
 * entry in a set.
 */
class TargetKey : public std::pair<proto::TargetId::Type, Database::IdT>
{

public:

  TargetKey (const proto::TargetId::Type type, const Database::IdT id)
  {
    first = type;
    second = id;
  }

  TargetKey (const proto::TargetId& id);

  /**
   * Converts the target back to proto format.
   */
  proto::TargetId ToProto () const;

};

/**
 * Finds combat targets for each fighter entity.
 */
void FindCombatTargets (Database& db, xaya::Random& rnd, const Context& ctx);

/**
 * Deals damage from combat and returns the target IDs of all fighters
 * that are now dead (and need to be handled accordingly).  This also
 * applies non-damage effects like slowing.
 */
std::set<TargetKey> DealCombatDamage (Database& db, DamageLists& dl,
                                      xaya::Random& rnd, const Context& ctx);

/**
 * Processes killed fighers from the given list, actually performing the
 * necessary database changes for having them dead.
 */
void ProcessKills (Database& db, DamageLists& dl, GroundLootTable& loot,
                   const std::set<TargetKey>& dead,
                   xaya::Random& rnd, const Context& ctx);

/**
 * Handles HP regeneration.
 */
void RegenerateHP (Database& db);

/**
 * Runs the three coupled steps to update HP at the beginning of computing
 * a block:  Dealing damage, handling kills and regenerating.
 */
void AllHpUpdates (Database& db, FameUpdater& fame, xaya::Random& rnd,
                   const Context& ctx);

} // namespace pxd

#endif // PXD_COMBAT_HPP
