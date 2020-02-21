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
 * Computes (from HP and RegenData protos) whether or not an entity
 * needs to regenerate HP.
 */
bool ComputeCanRegen (const proto::HP& hp, const proto::RegenData& regen);

/**
 * Computes the attack range of a fighter with the given combat data.
 * Returns zero if there are no attacks at all.
 */
HexCoord::IntT FindAttackRange (const proto::CombatData& cd);

} // namespace pxd

#endif // DATABASE_COMBAT_HPP
