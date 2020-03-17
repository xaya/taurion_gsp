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

#ifndef PXD_SERVICES_HPP
#define PXD_SERVICES_HPP

#include "database/account.hpp"
#include "database/amount.hpp"
#include "database/building.hpp"
#include "database/inventory.hpp"

#include <json/json.h>

#include <memory>

namespace pxd
{

/**
 * A particular service operation requested by a user in a move.  The individual
 * services (plus their details, e.g. refining and how much of what item
 * should be refined) are instances of subclasses.
 */
class ServiceOperation
{

private:

  /** The account triggering the service operation.  */
  Account& acc;

  /** The building in which the operation is happening.  */
  const Building& building;

protected:

  /** Database handle for upating building inventories (e.g. for refining).  */
  BuildingInventoriesTable& invTable;

  explicit ServiceOperation (Account& a, const Building& b,
                             BuildingInventoriesTable& i)
    : acc(a), building(b), invTable(i)
  {}

  /**
   * Returns true if the service is supported by the given building.
   */
  virtual bool IsSupported (const Building& b) const = 0;

  /**
   * Returns true if the operation is actually valid according to game
   * and move rules.
   */
  virtual bool IsValid () const = 0;

  /**
   * Returns the base cost (vCHI that are burnt) for this operation.
   */
  virtual Amount GetBaseCost () const = 0;

  /**
   * Executes the subclass-specific part of this operation, which is all updates
   * except for the vCHI cost.
   */
  virtual void ExecuteSpecific () = 0;

public:

  virtual ~ServiceOperation () = default;

  /**
   * Returns the building the operation is happening in.
   */
  const Building&
  GetBuilding () const
  {
    return building;
  }

  /**
   * Returns the account requesting this operation.
   */
  const Account&
  GetAccount () const
  {
    return acc;
  }

  /**
   * Fully executes the update corresponding to this operation.
   */
  void Execute ();

  /**
   * Tries to parse a service operation from JSON move data.  Returns nullptr
   * if the format is invalid or the operation would not be valid.
   */
  static std::unique_ptr<ServiceOperation> Parse (
      Account& acc, const Json::Value& data,
      BuildingsTable& buildings, BuildingInventoriesTable& inv);

};

} // namespace pxd

#endif // PXD_SERVICES_HPP
