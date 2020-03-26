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

#include "proto/roitems.hpp"

#include <glog/logging.h>

#include <string>

namespace pxd
{

/* ************************************************************************** */

class ServiceOperation::ContextRefs
{

private:

  const Context& ctx;
  AccountsTable& accounts;
  BuildingInventoriesTable& invTable;
  ItemCounts& cnt;

  friend class ServiceOperation;

public:

  explicit ContextRefs (const Context& c, AccountsTable& a,
                        BuildingInventoriesTable& i, ItemCounts& ic)
    : ctx(c), accounts(a), invTable(i), cnt(ic)
  {}

  ContextRefs () = delete;
  ContextRefs (const ContextRefs&) = delete;
  void operator= (const ContextRefs&) = delete;

};

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
  void ExecuteSpecific (xaya::Random& rnd) override;

public:

  explicit RefiningOperation (Account& a, BuildingsTable::Handle b,
                              const std::string& t,
                              const Inventory::QuantityT am,
                              const ContextRefs& refs);

  /**
   * Tries to parse a refining operation from the corresponding JSON move.
   * Returns a possibly invalid RefiningOperation instance or null if parsing
   * fails.
   */
  static std::unique_ptr<RefiningOperation> Parse (Account& acc,
                                                   BuildingsTable::Handle b,
                                                   const Json::Value& data,
                                                   const ContextRefs& refs);

};

RefiningOperation::RefiningOperation (Account& a, BuildingsTable::Handle b,
                                      const std::string& t,
                                      const Inventory::QuantityT am,
                                      const ContextRefs& refs)
  : ServiceOperation(a, std::move (b), refs),
    type(t), amount(am)
{
  const auto* itemData = RoItemDataOrNull (type);
  if (itemData == nullptr)
    {
      LOG (WARNING) << "Can't refine invalid item type " << type;
      refData = nullptr;
      return;
    }

  if (!itemData->has_refines ())
    {
      LOG (WARNING) << "Item type " << type << " can't be refined";
      refData = nullptr;
      return;
    }

  refData = &itemData->refines ();
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
RefiningOperation::ExecuteSpecific (xaya::Random& rnd)
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
RefiningOperation::Parse (Account& acc, BuildingsTable::Handle b,
                          const Json::Value& data,
                          const ContextRefs& refs)
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

  return std::make_unique<RefiningOperation> (acc, std::move (b),
                                              type.asString (),
                                              amount.asUInt64 (),
                                              refs);
}

/* ************************************************************************** */

/**
 * An "armour repair" operation.
 */
class RepairOperation : public ServiceOperation
{

private:

  /** The character repairing their armour.  */
  CharacterTable::Handle ch;

  /**
   * Returns the (signed) difference in armour HP that needs to be repaired.
   */
  int
  GetMissingHp () const
  {
    const int maxArmour = ch->GetRegenData ().max_hp ().armour ();
    const int curArmour = ch->GetHP ().armour ();
    return maxArmour - curArmour;
  }

protected:

  bool
  IsSupported (const Building& b) const override
  {
    return b.RoConfigData ().offered_services ().armour_repair ();
  }

  bool IsValid () const override;
  Amount GetBaseCost () const override;
  Json::Value SpecificToPendingJson () const override;
  void ExecuteSpecific (xaya::Random& rnd) override;

public:

  explicit RepairOperation (Account& a, BuildingsTable::Handle b,
                            CharacterTable::Handle c,
                            const ContextRefs& refs);

  /**
   * Tries to parse a repair operation from the corresponding JSON move.
   * Returns a possibly invalid RepairOperation instance or null if parsing
   * fails.
   */
  static std::unique_ptr<RepairOperation> Parse (Account& acc,
                                                 BuildingsTable::Handle b,
                                                 const Json::Value& data,
                                                 const ContextRefs& refs,
                                                 CharacterTable& characters);

};

RepairOperation::RepairOperation (Account& a, BuildingsTable::Handle b,
                                  CharacterTable::Handle c,
                                  const ContextRefs& refs)
  : ServiceOperation(a, std::move (b), refs), ch(std::move (c))
{}

bool
RepairOperation::IsValid () const
{
  if (ch == nullptr)
    {
      LOG (WARNING) << "Attempted armour repair for non-existant character";
      return false;
    }

  if (ch->GetOwner () != GetAccount ().GetName ())
    {
      LOG (WARNING)
          << GetAccount ().GetName () << " cannot repair armour of character "
          << ch->GetId () << " owned by " << ch->GetOwner ();
      return false;
    }

  if (!ch->IsInBuilding () || ch->GetBuildingId () != GetBuilding ().GetId ())
    {
      LOG (WARNING)
          << "Can't repair armour of character " << ch->GetId ()
          << " in building " << GetBuilding ().GetId ()
          << ", as the character isn't inside";
      return false;
    }

  if (ch->GetBusy () > 0)
    {
      LOG (WARNING)
          << "Character " << ch->GetId () << " is busy, can't repair armour";
      return false;
    }

  const int missingHp = GetMissingHp ();
  if (GetMissingHp () == 0)
    {
      LOG (WARNING)
          << "Character " << ch->GetId () << " has full armour, can't repair";
      return false;
    }
  CHECK_GT (missingHp, 0);

  return true;
}

Amount
RepairOperation::GetBaseCost () const
{
  /* There is some configured cost per HP (possibly fractional), and we round
     up the total cost.  */
  const Amount costMillis
      = GetMissingHp () * ctx.Params ().ArmourRepairCostMillis ();
  const Amount res = (costMillis + 999) / 1'000;
  CHECK_GT (res, 0);
  return res;
}

Json::Value
RepairOperation::SpecificToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["type"] = "armourrepair";
  res["character"] = IntToJson (ch->GetId ());

  return res;
}

void
RepairOperation::ExecuteSpecific (xaya::Random& rnd)
{
  LOG (INFO) << "Character " << ch->GetId () << " is repairing their armour";

  const auto hpPerBlock = ctx.Params ().ArmourRepairHpPerBlock ();
  const auto blocksBusy = (GetMissingHp () + (hpPerBlock - 1)) / hpPerBlock;
  CHECK_GT (blocksBusy, 0);

  ch->SetBusy (blocksBusy);
  ch->MutableProto ().mutable_armour_repair ();
}

std::unique_ptr<RepairOperation>
RepairOperation::Parse (Account& acc, BuildingsTable::Handle b,
                        const Json::Value& data,
                        const ContextRefs& refs,
                        CharacterTable& characters)
{
  CHECK (data.isObject ());
  if (data.size () != 3)
    return nullptr;

  Database::IdT charId;
  if (!IdFromJson (data["c"], charId))
    return nullptr;

  return std::make_unique<RepairOperation> (acc, std::move (b),
                                            characters.GetById (charId),
                                            refs);
}

/* ************************************************************************** */

/**
 * A reverse engineering operation (artefacts to blueprints).
 */
class RevEngOperation : public ServiceOperation
{

private:

  /** The type of artefact being reverse engineered.  */
  const std::string type;

  /** The number of artefacts to reverse engineer in this operation.  */
  const Inventory::QuantityT num;

  /**
   * The reveng data for the artefact type.  May be null if the item
   * type is invalid or it can't be refined.
   */
  const proto::ItemData::RevEngData* revEngData;

protected:

  bool
  IsSupported (const Building& b) const override
  {
    return b.RoConfigData ().offered_services ().reverse_engineering ();
  }

  Amount
  GetBaseCost () const override
  {
    return Inventory::Product (num, revEngData->cost ());
  }

  bool IsValid () const override;
  Json::Value SpecificToPendingJson () const override;
  void ExecuteSpecific (xaya::Random& rnd) override;

public:

  explicit RevEngOperation (Account& a, BuildingsTable::Handle b,
                            const std::string& t,
                            const Inventory::QuantityT n,
                            const ContextRefs& refs);

  /**
   * Tries to parse a reveng operation from the corresponding JSON move.
   * Returns a possibly invalid RevEngOperation instance or null if parsing
   * fails.
   */
  static std::unique_ptr<RevEngOperation> Parse (Account& acc,
                                                 BuildingsTable::Handle b,
                                                 const Json::Value& data,
                                                 const ContextRefs& refs);

};

RevEngOperation::RevEngOperation (Account& a, BuildingsTable::Handle b,
                                  const std::string& t,
                                  const Inventory::QuantityT n,
                                  const ContextRefs& refs)
  : ServiceOperation(a, std::move (b), refs),
    type(t), num(n)
{
  const auto* itemData = RoItemDataOrNull (type);
  if (itemData == nullptr)
    {
      LOG (WARNING) << "Can't reveng invalid item type " << type;
      revEngData = nullptr;
      return;
    }

  if (!itemData->has_reveng ())
    {
      LOG (WARNING) << "Item type " << type << " can't be reveng'ed";
      revEngData = nullptr;
      return;
    }

  revEngData = &itemData->reveng ();
}

bool
RevEngOperation::IsValid () const
{
  if (revEngData == nullptr)
    return false;

  if (num <= 0)
    return false;

  const auto buildingId = GetBuilding ().GetId ();
  const auto& name = GetAccount ().GetName ();

  const auto inv = invTable.Get (buildingId, name);
  const auto balance = inv->GetInventory ().GetFungibleCount (type);
  if (num > balance)
    {
      LOG (WARNING)
          << "Can't reveng " << num << " " << type
          << " as balance of " << name << " in building " << buildingId
          << " is only " << balance;
      return false;
    }

  return true;
}

Json::Value
RevEngOperation::SpecificToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["type"] = "reveng";

  Json::Value inp(Json::objectValue);
  inp[type] = IntToJson (num);
  res["input"] = inp;

  return res;
}

void
RevEngOperation::ExecuteSpecific (xaya::Random& rnd)
{
  const auto buildingId = GetBuilding ().GetId ();
  const auto& name = GetAccount ().GetName ();

  LOG (INFO)
      << name << " in building " << buildingId
      << " reverse engineers " << num << " " << type;

  auto invHandle = invTable.Get (buildingId, name);
  auto& inv = invHandle->GetInventory ();
  inv.AddFungibleCount (type, -num);

  const size_t numOptions = revEngData->possible_outputs_size ();
  CHECK_GT (numOptions, 0);

  for (unsigned trial = 0; trial < num; ++trial)
    {
      const size_t chosenOption = rnd.NextInt (numOptions);
      const std::string outType = revEngData->possible_outputs (chosenOption);

      const unsigned existingCount = itemCounts.GetFound (outType);
      const unsigned chance = ctx.Params ().RevEngSuccessChance (existingCount);
      const bool success = rnd.ProbabilityRoll (1, chance);
      LOG (INFO)
          << "Chosen output type " << outType
          << " has chance 1 / " << chance
          << "; success = " << success;

      if (success)
        {
          inv.AddFungibleCount (outType, 1);
          itemCounts.IncrementFound (outType);
        }
    }
}

std::unique_ptr<RevEngOperation>
RevEngOperation::Parse (Account& acc, BuildingsTable::Handle b,
                        const Json::Value& data,
                        const ContextRefs& refs)
{
  CHECK (data.isObject ());
  if (data.size () != 4)
    return nullptr;

  const auto& type = data["i"];
  if (!type.isString ())
    return nullptr;

  const auto& num = data["n"];
  if (!num.isUInt64 ())
    return nullptr;

  return std::make_unique<RevEngOperation> (acc, std::move (b),
                                            type.asString (),
                                            num.asUInt64 (),
                                            refs);
}

/* ************************************************************************** */

} // anonymous namespace

ServiceOperation::ServiceOperation (Account& a, BuildingsTable::Handle b,
                                    const ContextRefs& refs)
  : accounts(refs.accounts), acc(a), building(std::move (b)),
    ctx(refs.ctx), invTable(refs.invTable), itemCounts(refs.cnt)
{}

void
ServiceOperation::GetCosts (Amount& base, Amount& fee) const
{
  base = GetBaseCost ();

  /* Service is free if the building is an ancient one or if the owner is
     using their own building.  Even though they would get the fee back in
     the latter case, we still have to explicitly make it free so that they
     can execute the operation with a "tight budget" (that wouldn't allow
     temporarily paying the service fee).  */
  if (building->GetFaction () == Faction::ANCIENT
        || building->GetOwner () == acc.GetName ())
    {
      fee = 0;
      return;
    }

  /* Otherwise the service fee is determined as a percentage of the base cost,
     with the percentage given by the building configuration.  The result
     is rounded up.  */
  fee = (base * building->GetProto ().service_fee_percent () + 99) / 100;
}

Json::Value
ServiceOperation::ToPendingJson () const
{
  Json::Value res = SpecificToPendingJson ();
  CHECK (res.isObject ());

  res["building"] = IntToJson (building->GetId ());

  Amount base, fee;
  GetCosts (base, fee);

  Json::Value costs(Json::objectValue);
  costs["base"] = IntToJson (base);
  costs["fee"] = IntToJson (fee);
  res["cost"] = costs;

  return res;
}

void
ServiceOperation::Execute (xaya::Random& rnd)
{
  Amount base, fee;
  GetCosts (base, fee);

  acc.AddBalance (-base - fee);
  CHECK_GE (fee, 0);
  if (fee > 0)
    {
      auto owner = accounts.GetByName (building->GetOwner ());
      CHECK (owner != nullptr);
      CHECK_NE (owner->GetName (), acc.GetName ());
      owner->AddBalance (fee);
    }

  ExecuteSpecific (rnd);
}

std::unique_ptr<ServiceOperation>
ServiceOperation::Parse (Account& acc, const Json::Value& data,
                         const Context& ctx,
                         AccountsTable& accounts,
                         BuildingsTable& buildings,
                         BuildingInventoriesTable& inv,
                         CharacterTable& characters,
                         ItemCounts& cnt)
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

  const ContextRefs refs(ctx, accounts, inv, cnt);
  std::unique_ptr<ServiceOperation> op;
  if (type == "ref")
    op = RefiningOperation::Parse (acc, std::move (b), data, refs);
  else if (type == "fix")
    op = RepairOperation::Parse (acc, std::move (b), data, refs, characters);
  else if (type == "rve")
    op = RevEngOperation::Parse (acc, std::move (b), data, refs);
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

  if (!op->IsSupported (op->GetBuilding ()))
    {
      LOG (WARNING)
          << "Building " << op->GetBuilding ().GetId ()
          << " does not support service operation: " << data;
      return nullptr;
    }

  Amount base, fee;
  op->GetCosts (base, fee);
  if (base + fee > acc.GetBalance ())
    {
      LOG (WARNING)
          << "Service operation would cost " << (base + fee)
          << ", but " << acc.GetName () << " has only " << acc.GetBalance ()
          << ": " << data;
      return nullptr;
    }

  return op;
}

/* ************************************************************************** */

} // namespace pxd
