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

#include <set>

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

public:

  /** Set of attackers.  */
  using Attackers = std::set<Database::IdT>;

  /**
   * Constructs a damage list without specified height.  Such an instance
   * can be used to retrieve attackers, but cannot be used in operations
   * that depend on the height.
   */
  explicit DamageLists (Database& d)
    : db(d), height(NO_HEIGHT)
  {}

  explicit DamageLists (Database& d, const unsigned h)
    : db(d), height(h)
  {}

  DamageLists () = delete;
  DamageLists (const DamageLists&) = delete;
  void operator= (const DamageLists&) = delete;

  /**
   * Removes all entries on damage lists that are not from the last N blocks.
   * I.e., all with height <= (current height - N).
   */
  void RemoveOld (const unsigned n);

  /**
   * Adds (or refreshes) an entry for the given victim / attacker pair.
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
