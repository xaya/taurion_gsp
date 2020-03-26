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

#ifndef DATABASE_ITEMCOUNTS_HPP
#define DATABASE_ITEMCOUNTS_HPP

#include "database.hpp"

#include <string>

namespace pxd
{

/**
 * Wrapper class around the table of item counts in the database.
 */
class ItemCounts
{

private:

  /** The underlying database handle.  */
  Database& db;

public:

  explicit ItemCounts (Database& d)
    : db(d)
  {}

  ItemCounts () = delete;
  ItemCounts (const ItemCounts&) = delete;
  void operator= (const ItemCounts&) = delete;

  /**
   * Query how many of a given item have been found already.
   */
  unsigned GetFound (const std::string& name);

  /**
   * Increment the found counter of the given item.
   */
  void IncrementFound (const std::string& name);

};

} // namespace pxd

#endif // DATABASE_ITEMCOUNTS_HPP
