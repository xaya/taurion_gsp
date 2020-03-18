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

#include "services.hpp"

#include "jsonutils.hpp"

#include "proto/roconfig.hpp"

#include <glog/logging.h>

#include <string>

namespace pxd
{

namespace
{

/* ************************************************************************** */

/**
 * A refining operation.
 */
class RefiningOperation : public ServiceOperation
{

private:

  /** The type of resource being refined.  */
  const std::string type;

  /** The amount of raw resource being refined.  */
  const Inventory::QuantityT amount;

  /**
   * The refining data for the resource type.  May be null if the item
   * type is invalid or it can't be refined.
   */
  const proto::ItemData::RefiningData* refData;

  /**
   * Returns the number of refining steps this operation represents.  Assumes
   * that the operation is otherwise valid (e.g. we have refData).
   */
  unsigned
  GetSteps () const
  {
    return amount / refData->input_units ();
  }

protected:

  bool
  IsSupported (const Building& b) const override
  {
    return b.RoConfigData ().offered_services ().refining ();
  }

  Amount
  GetBaseCost () const override
  {
    return GetSteps () * refData->cost ();
  }

  bool IsValid () const override;
  Json::Value SpecificToPendingJson () const override;
  void ExecuteSpecific () override;

public:

  explicit RefiningOperation (Account& a, const Building& b,
                              const std::string& t,
                              const Inventory::QuantityT am,
                              BuildingInventoriesTable& i);

  /**
   * Tries to parse a refining operation from the corresponding JSON move.
   * Returns a possibly invalid RefiningOperation instance or null if parsing
   * fails.
   */
  static std::unique_ptr<RefiningOperation> Parse (Account& acc,
                                                   const Building& b,
                                                   const Json::Value& data,
                                                   BuildingInventoriesTable& i);

};

RefiningOperation::RefiningOperation (Account& a, const Building& b,
                                      const std::string& t,
                                      const Inventory::QuantityT am,
                                      BuildingInventoriesTable& i)
  : ServiceOperation(a, b, i),
    type(t), amount(am)
{
  const auto& itemData = RoConfigData ().fungible_items ();
  const auto mit = itemData.find (type);
  if (mit == itemData.end ())
    {
      LOG (WARNING) << "Can't refine invalid item type " << type;
      refData = nullptr;
      return;
    }

  if (!mit->second.has_refines ())
    {
      LOG (WARNING) << "Item type " << type << " can't be refined";
      refData = nullptr;
      return;
    }

  refData = &mit->second.refines ();
}

bool
RefiningOperation::IsValid () const
{
  if (refData == nullptr)
    return false;

  if (amount <= 0)
    return false;

  if (amount % refData->input_units () != 0)
    {
      LOG (WARNING)
          << "Invalid refinement input of " << amount << " " << type
          << ", the input for one step is " << refData->input_units ();
      return false;
    }

  const auto buildingId = GetBuilding ().GetId ();
  const auto& name = GetAccount ().GetName ();

  const auto inv = invTable.Get (buildingId, name);
  const auto balance = inv->GetInventory ().GetFungibleCount (type);
  if (amount > balance)
    {
      LOG (WARNING)
          << "Can't refine " << amount << " " << type
          << " as balance of " << name << " in building " << buildingId
          << " is only " << balance;
      return false;
    }

  return true;
}

Json::Value
RefiningOperation::SpecificToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["type"] = "refining";

  Json::Value inp(Json::objectValue);
  inp[type] = IntToJson (amount);

  CHECK (refData != nullptr);
  const unsigned steps = GetSteps ();
  Json::Value outp(Json::objectValue);
  for (const auto& out : refData->outputs ())
    outp[out.first] = IntToJson (steps * out.second);

  res["input"] = inp;
  res["output"] = outp;

  return res;
}

void
RefiningOperation::ExecuteSpecific ()
{
  const auto buildingId = GetBuilding ().GetId ();
  const auto& name = GetAccount ().GetName ();

  LOG (INFO)
      << name << " in building " << buildingId
      << " refines " << amount << " " << type;

  auto invHandle = invTable.Get (buildingId, name);
  auto& inv = invHandle->GetInventory ();
  inv.AddFungibleCount (type, -amount);

  const unsigned steps = GetSteps ();
  for (const auto& out : refData->outputs ())
    inv.AddFungibleCount (out.first, steps * out.second);
}

std::unique_ptr<RefiningOperation>
RefiningOperation::Parse (Account& acc, const Building& b,
                          const Json::Value& data,
                          BuildingInventoriesTable& inv)
{
  CHECK (data.isObject ());
  if (data.size () != 4)
    return nullptr;

  const auto& type = data["i"];
  if (!type.isString ())
    return nullptr;

  const auto& amount = data["n"];
  if (!amount.isUInt64 ())
    return nullptr;

  return std::make_unique<RefiningOperation> (acc, b, type.asString (),
                                              amount.asUInt64 (), inv);
}

/* ************************************************************************** */

} // anonymous namespace

Json::Value
ServiceOperation::ToPendingJson () const
{
  Json::Value res = SpecificToPendingJson ();
  CHECK (res.isObject ());

  res["building"] = IntToJson (building.GetId ());
  res["cost"] = IntToJson (GetBaseCost ());

  return res;
}

void
ServiceOperation::Execute ()
{
  acc.AddBalance (-GetBaseCost ());
  ExecuteSpecific ();
}

std::unique_ptr<ServiceOperation>
ServiceOperation::Parse (Account& acc, const Json::Value& data,
                         BuildingsTable& buildings,
                         BuildingInventoriesTable& inv)
{
  if (!data.isObject ())
    {
      LOG (WARNING) << "Invalid service operation: " << data;
      return nullptr;
    }

  Database::IdT buildingId;
  if (!IdFromJson (data["b"], buildingId))
    {
      LOG (WARNING) << "Invalid service operation: " << data;
      return nullptr;
    }

  auto b = buildings.GetById (buildingId);
  if (b == nullptr)
    {
      LOG (WARNING)
          << "Service operation requested in non-existant building "
          << buildingId;
      return nullptr;
    }

  const auto& typeVal = data["t"];
  if (!typeVal.isString ())
    {
      LOG (WARNING) << "Invalid service operation (no type): " << data;
      return nullptr;
    }
  const std::string type = typeVal.asString ();

  std::unique_ptr<ServiceOperation> op;
  if (type == "ref")
    op = RefiningOperation::Parse (acc, *b, data, inv);
  else
    {
      LOG (WARNING) << "Unknown service operation: " << type;
      return nullptr;
    }

  if (op == nullptr || !op->IsValid ())
    {
      LOG (WARNING) << "Invalid service operation: " << data;
      return nullptr;
    }

  if (!op->IsSupported (*b))
    {
      LOG (WARNING)
          << "Building " << b->GetId ()
          << " does not support service operation: " << data;
      return nullptr;
    }

  const Amount cost = op->GetBaseCost ();
  if (cost > acc.GetBalance ())
    {
      LOG (WARNING)
          << "Service operation would cost " << cost
          << ", but " << acc.GetName () << " has only " << acc.GetBalance ()
          << ": " << data;
      return nullptr;
    }

  return op;
}

/* ************************************************************************** */

} // namespace pxd
