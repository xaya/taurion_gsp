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

#ifndef PXD_COMBAT_HPP
#define PXD_COMBAT_HPP

#include "fame.hpp"

#include "database/damagelists.hpp"
#include "database/database.hpp"
#include "database/inventory.hpp"
#include "mapdata/basemap.hpp"
#include "proto/combat.pb.h"

#include <xayautil/random.hpp>

#include <vector>

namespace pxd
{

/**
 * Finds combat targets for each fighter entity.
 */
void FindCombatTargets (Database& db, xaya::Random& rnd);

/**
 * Deals damage from combat and returns the target IDs of all fighters
 * that are now dead (and need to be handled accordingly).
 */
std::vector<proto::TargetId> DealCombatDamage (Database& db, DamageLists& dl,
                                               xaya::Random& rnd);

/**
 * Processes killed fighers from the given list, actually performing the
 * necessary database changes for having them dead.
 */
void ProcessKills (Database& db, DamageLists& dl, GroundLootTable& loot,
                   const std::vector<proto::TargetId>& dead,
                   const BaseMap& map);

/**
 * Handles HP regeneration.
 */
void RegenerateHP (Database& db);

/**
 * Runs the three coupled steps to update HP at the beginning of computing
 * a block:  Dealing damage, handling kills and regenerating.
 */
void AllHpUpdates (Database& db, FameUpdater& fame, xaya::Random& rnd,
                   const BaseMap& map);

} // namespace pxd

#endif // PXD_COMBAT_HPP
