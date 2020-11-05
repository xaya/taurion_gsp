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

#ifndef PXD_TRADING_HPP
#define PXD_TRADING_HPP

#include "context.hpp"

#include "database/account.hpp"
#include "database/building.hpp"
#include "database/dex.hpp"
#include "database/inventory.hpp"

#include <json/json.h>

#include <memory>
#include <string>

namespace pxd
{

/**
 * A DEX trading operation (item transfer, new order or cancelled order).
 * This class is used to provide a uniform interface to all of these
 * operations for move and pending processing.
 */
class DexOperation
{

protected:

  class ContextRefs;

  const Context& ctx;

  AccountsTable& accounts;
  BuildingsTable& buildings;
  BuildingInventoriesTable& buildingInv;
  DexOrderTable& orders;

  /** The account triggering the operation.  */
  Account& account;

  /** The operation's raw move JSON (used for logs and error reporting).  */
  Json::Value rawMove;

  explicit DexOperation (Account& a, const ContextRefs& r);

public:

  virtual ~DexOperation () = default;

  const Account&
  GetAccount () const
  {
    return account;
  }

  /**
   * Returns true if the operation is actually valid according to game
   * and move rules.
   */
  virtual bool IsValid () const = 0;

  /**
   * Returns the pending JSON representation of this operation.
   */
  virtual Json::Value ToPendingJson () const = 0;

  /**
   * Fully executes the update corresponding to this operation.
   */
  virtual void Execute () = 0;

  /**
   * Tries to parse a DEX operation from JSON move data.  Returns nullptr
   * if the format is invalid.
   */
  static std::unique_ptr<DexOperation> Parse (
      Account& acc, const Json::Value& data,
      const Context& ctx,
      AccountsTable& accounts,
      BuildingsTable& buildings, BuildingInventoriesTable& inv,
      DexOrderTable& orders);

};

} // namespace pxd

#endif // PXD_TRADING_HPP
