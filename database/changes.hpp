/*
    GSP for the Taurion blockchain game
    Copyright (C) 2026  Autonomous Worlds Ltd

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

#ifndef DATABASE_CHANGES_HPP
#define DATABASE_CHANGES_HPP

#include "database.hpp"

#include "hexagonal/coord.hpp"
#include "mapdata/regionmap.hpp"

#include <xayautil/uint256.hpp>

#include <set>
#include <string>

namespace pxd
{

/**
 * The keys of all game entities changed by some blocks, read from the undo
 * data that libxayagame stores for each block (the SQLite changeset of its
 * state update).  An entity also counts as changed if data shown in its JSON
 * state changed in another table (e.g. the attackers of a character in the
 * damage lists), and the keys include entities that were deleted.  They may
 * also include some that did not actually change.
 */
struct ChangedKeys
{

  std::set<std::string> accounts;
  std::set<Database::IdT> buildings;
  std::set<Database::IdT> characters;
  std::set<HexCoord> groundLoot;
  std::set<Database::IdT> ongoings;
  std::set<RegionMap::IdT> regions;

  /*
   * Some characters change only in parts of their JSON:  all their changes
   * in all blocks were updates of their row in the characters table that
   * changed nothing but the columns of movement (x, y and volatilemv) and
   * combat (hp, target, effects, canregen and friendlytargets), and changes
   * of the damage lists with them as victim.  These are also in one or both
   * of the following subsets of characters.
   */

  /**
   * The characters with only partial changes, if any of them were of
   * movement.  Their JSON can have changed in its position and movement
   * (and in the region of an active miner, which shows that of its
   * position).
   */
  std::set<Database::IdT> movedCharacters;

  /**
   * The characters with only partial changes, if any of them were of combat
   * or the damage lists.  Their JSON can have changed in the HP, target,
   * effects and attackers of its combat data.
   */
  std::set<Database::IdT> combatCharacters;

  /**
   * Reads the keys changed by all blocks after the given one, up to the
   * current state of the database at the given height.  Returns false if
   * they are not available, i.e. the block is not part of the current chain,
   * too far back or already pruned, or if the changes affect all entities
   * (like an update of the roconfig).  The keys are unspecified then.
   */
  bool ReadSince (Database& db, const xaya::uint256& block, unsigned height);

};

} // namespace pxd

#endif // DATABASE_CHANGES_HPP
