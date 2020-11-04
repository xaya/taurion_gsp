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

#include "trading.hpp"

#include "jsonutils.hpp"

#include <glog/logging.h>

namespace pxd
{

/* ************************************************************************** */

/**
 * Collection of basic "context" references that we need for DEX orders.
 * This is just used to simplify passing them around.
 */
class DexOperation::ContextRefs
{

private:

  const Context& ctx;

  AccountsTable& accounts;
  BuildingsTable& buildings;
  BuildingInventoriesTable& buildingInv;

  explicit ContextRefs (const Context& c,
                        AccountsTable& a,
                        BuildingsTable& b, BuildingInventoriesTable& i)
    : ctx(c), accounts(a), buildings(b), buildingInv(i)
  {}

  friend class DexOperation;

};

namespace
{

/* ************************************************************************** */

/**
 * DEX operation that explicitly specifies a building, item type
 * and quantity.  This shares logic between transfers, bids and asks.
 */
class ItemOperation : public DexOperation
{

protected:

  /** The building ID this is taking place in.  */
  const Database::IdT building;

  /** The item type this is for.  */
  const std::string item;

  /** The amount of item being operated on.  */
  const Quantity quantity;

  explicit ItemOperation (Account& a, const ContextRefs& r,
                          const Database::IdT b, const std::string& i,
                          const Quantity n)
    : DexOperation(a, r), building(b), item(i), quantity(n)
  {}

  /**
   * Checks if the general data pieces are valid (building exists,
   * item exists and quantity is within range).
   */
  bool IsItemOperationValid () const;

  /**
   * Returns a base pending JSON object for the generic pieces of data
   * in this item operation.
   */
  Json::Value PendingItemOperation () const;

};

bool
ItemOperation::IsItemOperationValid () const
{
  const auto b = buildings.GetById (building);
  if (b == nullptr)
    {
      LOG (WARNING)
          << "Invalid building " << building << " in operation:\n" << rawMove;
      return false;
    }
  if (b->GetProto ().foundation ())
    {
      LOG (WARNING)
          << "Invalid operation in foundation " << building << ":\n" << rawMove;
      return false;
    }

  if (ctx.RoConfig ().ItemOrNull (item) == nullptr)
    {
      LOG (WARNING)
          << "Invalid item '" << item << "' in operation:\n" << rawMove;
      return false;
    }

  /* The quantity is already checked for being in range (0, MAX_QUANTITY]
     when parsing the instance.  */

  return true;
}

Json::Value
ItemOperation::PendingItemOperation () const
{
  Json::Value res(Json::objectValue);
  res["building"] = IntToJson (building);
  res["item"] = item;
  res["num"] = IntToJson (quantity);
  return res;
}

/* ************************************************************************** */

/**
 * A direct item transfer between user accounts inside a building.
 */
class TransferOperation : public ItemOperation
{

private:

  /** The recipient account.  */
  const std::string recipient;

public:

  explicit TransferOperation (Account& a, const ContextRefs& r,
                              const Database::IdT b, const std::string& i,
                              const Quantity n, const std::string& rec)
    : ItemOperation(a, r, b, i, n), recipient(rec)
  {}

  bool IsValid () const override;
  Json::Value ToPendingJson () const override;
  void Execute () override;

};

bool
TransferOperation::IsValid () const
{
  if (!IsItemOperationValid ())
    return false;

  const auto inv = buildingInv.Get (building, account.GetName ());
  const Quantity got = inv->GetInventory ().GetFungibleCount (item);
  if (got < quantity)
    {
      LOG (WARNING)
          << "User " << account.GetName () << " has only " << got
          << " of " << item << " in building " << building
          << ", cannot transfer:\n" << rawMove;
      return false;
    }

  return true;
}

Json::Value
TransferOperation::ToPendingJson () const
{
  Json::Value res = PendingItemOperation ();
  res["op"] = "transfer";
  res["to"] = recipient;
  return res;
}

void
TransferOperation::Execute ()
{
  LOG (INFO)
      << "Transferring " << quantity << " of " << item
      << " inside " << building
      << " from " << account.GetName () << " to " << recipient;

  if (accounts.GetByName (recipient) == nullptr)
    accounts.CreateNew (recipient);

  buildingInv.Get (building, account.GetName ())
      ->GetInventory ().AddFungibleCount (item, -quantity);
  buildingInv.Get (building, recipient)
      ->GetInventory ().AddFungibleCount (item, quantity);
}

/* ************************************************************************** */

} // anonymous namespace

DexOperation::DexOperation (Account& a, const ContextRefs& r)
  : ctx(r.ctx),
    accounts(r.accounts), buildings(r.buildings), buildingInv(r.buildingInv),
    account(a)
{}

std::unique_ptr<DexOperation>
DexOperation::Parse (Account& acc, const Json::Value& data,
                     const Context& ctx,
                     AccountsTable& accounts,
                     BuildingsTable& buildings, BuildingInventoriesTable& inv)
{
  if (!data.isObject ())
    return nullptr;

  if (data.size () != 4)
    return nullptr;

  Database::IdT building;
  if (!IdFromJson (data["b"], building))
    return nullptr;

  const auto& itmVal = data["i"];
  if (!itmVal.isString ())
    return nullptr;
  const std::string item = itmVal.asString ();

  Quantity quantity;
  if (!QuantityFromJson (data["n"], quantity))
    return nullptr;

  const auto& recvVal = data["t"];
  if (!recvVal.isString ())
    return nullptr;
  const std::string recipient = recvVal.asString ();

  const ContextRefs refs(ctx, accounts, buildings, inv);
  auto op = std::make_unique<TransferOperation> (acc, refs, building, item,
                                                 quantity, recipient);
  op->rawMove = data;
  return op;
}

/* ************************************************************************** */

} // namespace pxd
