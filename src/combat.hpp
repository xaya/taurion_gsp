#ifndef PXD_COMBAT_HPP
#define PXD_COMBAT_HPP

#include "database/database.hpp"
#include "proto/combat.pb.h"

#include <xayagame/random.hpp>

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
std::vector<proto::TargetId> DealCombatDamage (Database& db, xaya::Random& rnd);

/**
 * Processes killed fighers from the given list, actually performing the
 * necessary database changes for having them dead.
 */
void ProcessKills (Database& db, const std::vector<proto::TargetId>& dead);

/**
 * Handles HP regeneration.
 */
void RegenerateHP (Database& db);

/**
 * Runs the three coupled steps to update HP at the beginning of computing
 * a block:  Dealing damage, handling kills and regenerating.
 */
void AllHpUpdates (Database& db, xaya::Random& rnd);

} // namespace pxd

#endif // PXD_COMBAT_HPP
