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

#include "context.hpp"

#include "database/account.hpp"
#include "database/amount.hpp"
#include "database/building.hpp"
#include "database/character.hpp"
#include "database/inventory.hpp"
#include "database/itemcounts.hpp"
#include "database/ongoing.hpp"
#include "proto/roconfig.hpp"

#include <xayautil/random.hpp>

#include <json/json.h>

#include <memory>
#include <string>

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

  /**
   * Account table, which is needed to look up and modify the building owner
   * account when fees are paid.
   */
  AccountsTable& accounts;

  /** Database handle for upating building inventories (e.g. for refining).  */
  BuildingInventoriesTable& invTable;

  /** Database table for ongoing operations.  */
  OngoingsTable& ongoings;

  /** The account triggering the service operation.  */
  Account& acc;

  /**
   * The building in which the operation is happening.  Might be null if
   * it is e.g. a mobile refinery operation.
   */
  BuildingsTable::Handle building;

  /**
   * The character that is doing the operation if it is a mobile refinery
   * (and not inside a building).  This is just a reference and not a handle,
   * as it will be used from within character processing where an instance
   * already exists on the outside.
   */
  Character* character;

  /**
   * If this is inside a building, the handle to the account's inventory
   * inside the building.
   */
  BuildingInventoriesTable::Handle buildingInv;

  /** The operation's raw move JSON (used for logs and error reporting).  */
  Json::Value rawMove;

  /**
   * Computes the base and service cost.  The base cost is burnt (and defined
   * by the service operation subclasses), while the service fee is sent
   * to the building's owner and controlled by them.
   */
  void GetCosts (Amount& base, Amount& fee) const;

protected:

  /**
   * Utility class that wraps all database table and context references
   * needed to construct a ServiceOperation instance, so we can easily pass
   * them around without ever-growing argument lists.
   */
  class ContextRefs;

  /** Context for parameters and such.  */
  const Context& ctx;

  /** Database handle for item-count tables.  */
  ItemCounts& itemCounts;

  /**
   * Constructs an instance for an operation happening inside a building.
   */
  explicit ServiceOperation (Account& a, BuildingsTable::Handle b,
                             const ContextRefs& refs);

  /**
   * Constructs an instance for an operation happening by a character
   * (e.g. mobile refinery).
   */
  explicit ServiceOperation (Account& a, Character& c,
                             const ContextRefs& refs);

  /**
   * Returns a basic string representation of this operation's "location"
   * for log messages; this is either the account name and building, or
   * the character doing it.
   */
  std::string GetLocationInfo () const;

  /**
   * Returns the inventory to use for operations.
   */
  const Inventory& GetBaseInventory () const;

  /**
   * Returns the inventory to use for operations that work with inventories.
   * This is the account's building inventory for operations inside buildings,
   * and the character inventory for stand-alone operations like mobile
   * refining.
   */
  Inventory& GetBaseInventory ();

  /**
   * Creates a new ongoing operation entry and also sets the (mandatory)
   * start height on it already.
   */
  OngoingsTable::Handle CreateOngoing ();

  /**
   * Returns true if the service is supported by the given building.
   */
  virtual bool IsSupported (const Building& b) const = 0;

  /**
   * Returns true if the operation is supported by the given character
   * (e.g. mobile refinery) if done outside a building.  By default this
   * returns false, as that is what most operations need.
   */
  virtual bool
  IsSupported (const Character& c) const
  {
    return false;
  }

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
  virtual void ExecuteSpecific (xaya::Random& rnd) = 0;

  /**
   * Converts the subclass-specific data of this operation (not including
   * e.g. building or cost) to JSON for the pending state.  Must return
   * a JSON object.
   */
  virtual Json::Value SpecificToPendingJson () const = 0;

public:

  virtual ~ServiceOperation () = default;

  /**
   * Returns the building the operation is happening in.  Must only be
   * called if it is in a building.
   */
  const Building& GetBuilding () const;

  /**
   * Returns the account requesting this operation.
   */
  const Account&
  GetAccount () const
  {
    return acc;
  }

  /**
   * Performs some additional validations (over what Parse already does)
   * and returns true if the operation is fully valid (i.e. should be executed
   * when confirmed / reported in the pending state).
   */
  bool IsFullyValid () const;

  /**
   * Returns a JSON representation of this operation for pending moves.
   */
  Json::Value ToPendingJson () const;

  /**
   * Fully executes the update corresponding to this operation.
   */
  void Execute (xaya::Random& rnd);

  /**
   * Tries to parse a service operation from JSON move data.  Returns nullptr
   * if the format is invalid.
   */
  static std::unique_ptr<ServiceOperation> Parse (
      Account& acc, const Json::Value& data,
      const Context& ctx,
      AccountsTable& accounts,
      BuildingsTable& buildings, BuildingInventoriesTable& inv,
      CharacterTable& characters,
      ItemCounts& cnt,
      OngoingsTable& ong);

  /**
   * Tries to parse and return a refining operation on a character, i.e.
   * using a mobile refinery.
   */
  static std::unique_ptr<ServiceOperation> ParseMobileRefining (
      Account& acc, Character& c, const Json::Value& data,
      const Context& ctx,
      AccountsTable& accounts,
      BuildingInventoriesTable& inv,
      ItemCounts& cnt,
      OngoingsTable& ong);

};

/**
 * Returns the number of blocks for constructing one item of the
 * given type.
 */
unsigned GetConstructionBlocks (const std::string& itm, const Context& ctx);

} // namespace pxd

#endif // PXD_SERVICES_HPP
