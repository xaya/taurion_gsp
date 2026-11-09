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

#ifndef DATABASE_DAMAGELISTS_HPP
#define DATABASE_DAMAGELISTS_HPP

#include "database.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace pxd
{

/**
 * Wrapper class for access to the damage lists in the database.
 */
class DamageLists
{

private:

  /** Magic height value meaning that we do not have a height.  */
  static constexpr unsigned NO_HEIGHT = static_cast<unsigned> (-1);

  /** The underlying database handle.  */
  Database& db;

  /**
   * The current block height.  This is fixed at creation time and stored
   * here for ease of use, so that we do not have to pass it around all
   * the time.
   */
  const unsigned height;

  /** The age (in blocks) at which a hit refreshes an existing entry.  */
  const unsigned refresh;

  /**
   * The entries added through this instance, as (victim, attacker) and as
   * (attacker, victim) pairs.  They are only written to the database when
   * the instance is destroyed, so that those removed again before (e.g.
   * when both characters die in the same battle) never are.
   */
  std::set<std::pair<Database::IdT, Database::IdT>> pendingByVictim;
  std::set<std::pair<Database::IdT, Database::IdT>> pendingByAttacker;

  /**
   * Removes the pending entries in which the given character is first
   * in the pairs of one set, and the matching ones from the other set.
   */
  static void RemovePending (
      std::set<std::pair<Database::IdT, Database::IdT>>& by,
      std::set<std::pair<Database::IdT, Database::IdT>>& other,
      Database::IdT id);

public:

  /** Set of attackers.  */
  using Attackers = std::set<Database::IdT>;

  /**
   * Constructs a damage list without specified height.  Such an instance
   * can be used to retrieve attackers, but cannot be used in operations
   * that depend on the height.
   */
  explicit DamageLists (Database& d)
    : db(d), height(NO_HEIGHT), refresh(1)
  {}

  /**
   * Constructs a damage list for the given height.  A hit refreshes the
   * height of an existing entry only once it is at least r blocks old.
   */
  explicit DamageLists (Database& d, const unsigned h, const unsigned r = 1)
    : db(d), height(h), refresh(std::max (r, 1u))
  {}

  ~DamageLists ();

  DamageLists () = delete;
  DamageLists (const DamageLists&) = delete;
  void operator= (const DamageLists&) = delete;

  /**
   * Removes all entries on damage lists that are not from the last N blocks.
   * I.e., all with height <= (current height - N).  This must be done before
   * entries are added through this instance.
   */
  void RemoveOld (const unsigned n);

  /**
   * Adds an entry for the given victim / attacker pair, or refreshes its
   * height if it is at least the refresh age old.  The entry is seen by
   * GetAttackers and removed by RemoveCharacter of this instance right away,
   * but written to the database only when the instance is destroyed.
   */
  void AddEntry (Database::IdT victim, Database::IdT attacker);

  /**
   * Removes all entries involving the given character (either as victim or
   * attacker).  This is used for cleaning up the database when a character
   * has been killed.
   */
  void RemoveCharacter (Database::IdT id);

  /**
   * Returns all attackers on the damage list for the given ID.
   */
  Attackers GetAttackers (Database::IdT victim) const;

};

} // namespace pxd

#endif // DATABASE_DAMAGELISTS_HPP
