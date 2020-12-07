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
  DexOrderTable& orders;
  DexHistoryTable& history;

  explicit ContextRefs (const Context& c,
                        AccountsTable& a,
                        BuildingsTable& b, BuildingInventoriesTable& i,
                        DexOrderTable& o, DexHistoryTable& h)
    : ctx(c), accounts(a), buildings(b), buildingInv(i), orders(o), history(h)
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
   * Returns an inventory handle for the account of this order
   * inside the building.
   */
  BuildingInventoriesTable::Handle
  GetInv () const
  {
    return buildingInv.Get (building, account.GetName ());
  }

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

  const Quantity got = GetInv ()->GetInventory ().GetFungibleCount (item);
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

  GetInv ()->GetInventory ().AddFungibleCount (item, -quantity);
  buildingInv.Get (building, recipient)
      ->GetInventory ().AddFungibleCount (item, quantity);
}

/* ************************************************************************** */

/**
 * A DEX operation to place a new order (either bid or ask).
 */
class NewOrderOperation : public ItemOperation
{

private:

  /** String representation of this operation (for the pending JSON "op").  */
  const std::string op;

  /**
   * Pays the given amount of Cubits to the given user name.  This takes
   * care of handling the special case that the recipient is the account
   * performing the current operation, in which case we must not instantiate
   * a second Account instance.
   */
  void PayCoins (const std::string& recipient, Amount cost) const;

protected:

  /** The price of the order (in Cubits per unit).  */
  const Amount price;

  /**
   * Pays the given amount in Cubits to the seller of an item (recipient),
   * taking fees into account and paying them to the building owner / burning
   * them instead.
   */
  void PayToSellerAndFee (const std::string& recipient, Amount cost) const;

public:

  explicit NewOrderOperation (Account& a, const ContextRefs& r,
                              const std::string& o,
                              const Database::IdT b, const std::string& i,
                              const Quantity n, const Amount p)
    : ItemOperation(a, r, b, i, n), op(o), price(p)
  {}

  Json::Value ToPendingJson () const override;

};

void
NewOrderOperation::PayCoins (const std::string& recipient,
                             const Amount cost) const
{
  if (cost == 0)
    return;

  if (recipient == account.GetName ())
    account.AddBalance (cost);
  else
    {
      auto a = accounts.GetByName (recipient);
      if (a == nullptr)
        a = accounts.CreateNew (recipient);
      a->AddBalance (cost);
    }
}

void
NewOrderOperation::PayToSellerAndFee (const std::string& recipient,
                                      const Amount cost) const
{
  CHECK_GE (cost, 0);

  auto b = buildings.GetById (building);
  CHECK (b != nullptr);

  const int baseBps = ctx.RoConfig ()->params ().dex_fee_bps ();
  const int ownerBps = b->GetProto ().config ().dex_fee_bps ();
  const int totalBps = baseBps + ownerBps;

  if (b->GetFaction () == Faction::ANCIENT)
    CHECK_EQ (ownerBps, 0);

  /* The total is rounded up to the next Cubit.  This ensures that sellers
     cannot try to dodge fees completely by splitting up orders, but it also
     ensures that the most fee paid is one Cubit per fill.

     The fee for the building owner is rounded down, so that there is no
     extra incentive to split up orders into small parts and gain from
     rounding up.  */

  const Amount total = (cost * totalBps + 9'999) / 10'000;
  const Amount owner = (cost * ownerBps) / 10'000;
  const Amount payout = cost - total;
  CHECK_GE (payout, 0);
  CHECK_LE (owner + payout, cost);

  /* We need to make sure GetOwner is not called in case of an ancient
     building, thus check the amount (which will be zero for ancient
     buildings per code above) even though it will be checked
     again in PayCoins.  */
  if (owner > 0)
    PayCoins (buildings.GetById (building)->GetOwner (), owner);
  PayCoins (recipient, payout);
}

Json::Value
NewOrderOperation::ToPendingJson () const
{
  Json::Value res = PendingItemOperation ();
  res["op"] = op;
  res["price"] = IntToJson (price);
  return res;
}

/* ************************************************************************** */

/**
 * An operation to place a bid (buy order).
 */
class BidOperation : public NewOrderOperation
{

public:

  explicit BidOperation (Account& a, const ContextRefs& r,
                         const Database::IdT b, const std::string& i,
                         const Quantity n, const Amount p)
    : NewOrderOperation(a, r, "bid", b, i, n, p)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
BidOperation::IsValid () const
{
  if (!IsItemOperationValid ())
    return false;

  if (QuantityProduct (quantity, price) > account.GetBalance ())
    {
      LOG (WARNING)
          << "User " << account.GetName ()
          << " has only " << account.GetBalance () << " coins,"
          << " can't place buy order:\n" << rawMove;
      return false;
    }

  return true;
}

void
BidOperation::Execute ()
{
  auto m = orders.QueryToMatchBid (building, item, price);
  Quantity remaining = quantity;
  while (m.Step () && remaining > 0)
    {
      auto o = orders.GetFromResult (m);
      const Quantity cur = std::min (remaining, o->GetQuantity ());

      /* The items sold have already been deducted from the seller's
         account when the order was created.  So we just have to credit
         them to the buyer, and transfer the Cubit payment.  */

      GetInv ()->GetInventory ().AddFungibleCount (item, cur);

      const Amount cost = QuantityProduct (cur, o->GetPrice ()).Extract ();
      PayToSellerAndFee (o->GetAccount (), cost);
      account.AddBalance (-cost);

      history.RecordTrade (ctx.Height (), ctx.Timestamp (),
                           building, item, cur, o->GetPrice (),
                           o->GetAccount (), account.GetName ());

      o->ReduceQuantity (cur);
      remaining -= cur;
    }

  CHECK_GE (remaining, 0);
  if (remaining == 0)
    return;

  auto o = orders.CreateNew (building, account.GetName (),
                             DexOrder::Type::BID, item, remaining, price);
  VLOG (1)
      << "Placing remaining " << remaining
      << " units of order onto the orderbook: "
      << "ID " << o->GetId () << "\n"
      << rawMove;
  account.AddBalance (-QuantityProduct (remaining, price).Extract ());
}

/**
 * An operation to place an ask (sell order).
 */
class AskOperation : public NewOrderOperation
{

public:

  explicit AskOperation (Account& a, const ContextRefs& r,
                         const Database::IdT b, const std::string& i,
                         const Quantity n, const Amount p)
    : NewOrderOperation(a, r, "ask", b, i, n, p)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
AskOperation::IsValid () const
{
  if (!IsItemOperationValid ())
    return false;

  const Quantity got = GetInv ()->GetInventory ().GetFungibleCount (item);
  if (got < quantity)
    {
      LOG (WARNING)
          << "User " << account.GetName () << " has only " << got
          << " of " << item << " in building " << building
          << ", cannot sell:\n" << rawMove;
      return false;
    }

  return true;
}

void
AskOperation::Execute ()
{
  auto m = orders.QueryToMatchAsk (building, item, price);
  Quantity remaining = quantity;
  while (m.Step () && remaining > 0)
    {
      auto o = orders.GetFromResult (m);
      const Quantity cur = std::min (remaining, o->GetQuantity ());

      /* The Cubits paid to the seller (from the existing bid order)
         have already been deducted from the buyer's account when the
         bid was placed.  Thus we just have to pay the seller (executing
         this order) and transfer the items.  */

      buildingInv.Get (building, o->GetAccount ())
          ->GetInventory ().AddFungibleCount (item, cur);
      GetInv ()->GetInventory ().AddFungibleCount (item, -cur);

      const Amount cost = QuantityProduct (cur, o->GetPrice ()).Extract ();
      PayToSellerAndFee (account.GetName (), cost);

      history.RecordTrade (ctx.Height (), ctx.Timestamp (),
                           building, item, cur, o->GetPrice (),
                           account.GetName (), o->GetAccount ());

      o->ReduceQuantity (cur);
      remaining -= cur;
    }

  CHECK_GE (remaining, 0);
  if (remaining == 0)
    return;

  auto o = orders.CreateNew (building, account.GetName (),
                             DexOrder::Type::ASK, item, remaining, price);
  VLOG (1)
      << "Placing remaining " << remaining
      << " units of order onto the orderbook: "
      << "ID " << o->GetId () << "\n"
      << rawMove;
  GetInv ()->GetInventory ().AddFungibleCount (item, -remaining);
}

/* ************************************************************************** */

/**
 * An operation that cancels an existing DEX order by ID.
 */
class CancelOrderOperation : public DexOperation
{

private:

  /** The ID of the order to cancel.  */
  const Database::IdT id;

public:

  explicit CancelOrderOperation (Account& a, const ContextRefs& r,
                                 const Database::IdT o)
    : DexOperation(a, r), id(o)
  {}

  bool IsValid () const override;
  Json::Value ToPendingJson () const override;
  void Execute () override;

};

bool
CancelOrderOperation::IsValid () const
{
  auto o = orders.GetById (id);
  if (o == nullptr)
    {
      LOG (WARNING) << "Invalid order to cancel: " << id;
      return false;
    }

  if (o->GetAccount () != account.GetName ())
    {
      LOG (WARNING)
          << "Order " << id << " is owned by " << o->GetAccount ()
          << " and can't be cancelled by " << account.GetName ()
          << ":\n" << rawMove;
      return false;
    }

  return true;
}

Json::Value
CancelOrderOperation::ToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["op"] = "cancel";
  res["order"] = IntToJson (id);
  return res;
}

void
CancelOrderOperation::Execute ()
{
  auto o = orders.GetById (id);
  CHECK (o != nullptr) << "Order does not exist: " << id;

  LOG (INFO)
      << "Cancelling DEX order " << id
      << " of " << o->GetAccount () << " in building " << o->GetBuilding ();

  switch (o->GetType ())
    {
    case DexOrder::Type::BID:
      {
        const QuantityProduct prod(o->GetQuantity (), o->GetPrice ());
        const Amount cost = prod.Extract ();
        VLOG (1) << "Refunding " << cost << " coins to " << o->GetAccount ();
        account.AddBalance (cost);
        break;
      }

    case DexOrder::Type::ASK:
      VLOG (1)
          << "Refunding " << o->GetQuantity () << " of " << o->GetItem ()
          << " to " << o->GetAccount () << " in " << o->GetBuilding ();
      buildingInv.Get (o->GetBuilding (), o->GetAccount ())
          ->GetInventory ().AddFungibleCount (o->GetItem (), o->GetQuantity ());
      break;

    default:
      LOG (FATAL) << "Invalid order type: " << static_cast<int> (o->GetType ());
    }

  o->Delete ();
}

/* ************************************************************************** */

} // anonymous namespace

DexOperation::DexOperation (Account& a, const ContextRefs& r)
  : ctx(r.ctx),
    accounts(r.accounts),
    buildings(r.buildings), buildingInv(r.buildingInv),
    orders(r.orders), history(r.history),
    account(a)
{}

std::unique_ptr<DexOperation>
DexOperation::Parse (Account& acc, const Json::Value& data,
                     const Context& ctx,
                     AccountsTable& accounts,
                     BuildingsTable& buildings, BuildingInventoriesTable& inv,
                     DexOrderTable& dex, DexHistoryTable& hist)
{
  if (!data.isObject ())
    return nullptr;

  const ContextRefs refs(ctx, accounts, buildings, inv, dex, hist);
  std::unique_ptr<DexOperation> op;

  /* Order cancellation is a special case.  */
  if (data.size () == 1)
    {
      Database::IdT id;
      if (!IdFromJson (data["c"], id))
        return nullptr;

      op = std::make_unique<CancelOrderOperation> (acc, refs, id);
    }

  /* All other cases have a similar structure.  */
  else
    {
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

      /* Since we checked above that there are exactly four members
         in the JSON object, at most one of the following if statements
         can ever be true.  If none is true, then we end up with op being
         still null at the end of the function.  */

      const auto& recvVal = data["t"];
      if (recvVal.isString ())
        {
          CHECK (op == nullptr);
          const auto recv = recvVal.asString ();
          op = std::make_unique<TransferOperation> (acc, refs, building, item,
                                                    quantity, recv);
        }

      Amount price;
      if (CoinAmountFromJson (data["bp"], price))
        {
          CHECK (op == nullptr);
          op = std::make_unique<BidOperation> (acc, refs, building, item,
                                               quantity, price);
        }
      if (CoinAmountFromJson (data["ap"], price))
        {
          CHECK (op == nullptr);
          op = std::make_unique<AskOperation> (acc, refs, building, item,
                                               quantity, price);
        }
    }

  if (op != nullptr)
    op->rawMove = data;

  return op;
}

/* ************************************************************************** */

} // namespace pxd
