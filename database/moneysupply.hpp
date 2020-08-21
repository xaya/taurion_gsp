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

#ifndef DATABASE_MONEYSUPPLY_HPP
#define DATABASE_MONEYSUPPLY_HPP

#include "amount.hpp"
#include "database.hpp"

#include <set>
#include <string>

namespace pxd
{

/**
 * Wrapper class around the database table holding data about
 * the money supply.
 */
class MoneySupply
{

private:

  /** The underlying database handle.  */
  Database& db;

public:

  explicit MoneySupply (Database& d)
    : db(d)
  {}

  MoneySupply () = delete;
  MoneySupply (const MoneySupply&) = delete;
  void operator= (const MoneySupply&) = delete;

  /**
   * Returns the value of one accounting entry.  This CHECK-fails if the
   * key is invalid (as we have a well-defined and initialised set of
   * rows at all times).
   */
  Amount Get (const std::string& key);

  /**
   * Increments the amount for one accounting entry.
   */
  void Increment (const std::string& key, Amount value);

  /**
   * Initialises the database, putting in all entries that are valid
   * with initial amounts (e.g. zero for the burnsale).
   */
  void InitialiseDatabase ();

  /**
   * Returns the set of valid keys.
   */
  static const std::set<std::string>& GetValidKeys ();

};

} // namespace pxd

#endif // DATABASE_MONEYSUPPLY_HPP
