#ifndef PXD_COMBAT_HPP
#define PXD_COMBAT_HPP

#include "database/database.hpp"

#include <xayagame/random.hpp>

namespace pxd
{

/**
 * Finds combat targets for each fighter entity.
 */
void FindCombatTargets (Database& db, xaya::Random& rnd);

} // namespace pxd

#endif // PXD_COMBAT_HPP
