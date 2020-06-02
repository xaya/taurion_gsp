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
  OngoingsTable& ongoings;

  friend class ServiceOperation;

public:

  explicit ContextRefs (const Context& c, AccountsTable& a,
                        BuildingInventoriesTable& i, ItemCounts& ic,
                        OngoingsTable& ong)
    : ctx(c), accounts(a), invTable(i), cnt(ic), ongoings(ong)
  {}

  ContextRefs () = delete;
  ContextRefs (const ContextRefs&) = delete;
  void operator= (const ContextRefs&) = delete;

};

namespace
{

/**
 * Basic parser routine for the common case of (item type, amount) as additional
 * data in the JSON.  This is shared between refinery, reveng, blueprint copy
 * and construction.
 */
template <typename T>
  std::unique_ptr<T>
  ParseItemAmount (Account& acc, BuildingsTable::Handle b,
                   const Json::Value& data,
                   const ServiceOperation::ContextRefs& refs)
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

  return std::make_unique<T> (acc, std::move (b),
                              type.asString (), amount.asUInt64 (),
                              refs);
}

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
  const proto::RefiningData* refData;

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
    return cfg.Building (b.GetType ()).offered_services ().refining ();
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

};

RefiningOperation::RefiningOperation (Account& a, BuildingsTable::Handle b,
                                      const std::string& t,
                                      const Inventory::QuantityT am,
                                      const ContextRefs& refs)
  : ServiceOperation(a, std::move (b), refs),
    type(t), amount(am)
{
  const auto* itemData = cfg.ItemOrNull (type);
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
    return cfg.Building (b.GetType ()).offered_services ().armour_repair ();
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

  if (ch->IsBusy ())
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

  /* If there are no missing HP, then the operation is invalid.  But through
     getserviceinfo, the cost can still be calculated, and will then just
     be zero.  */
  CHECK_GE (res, 0);

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

  auto op = CreateOngoing ();
  ch->MutableProto ().set_ongoing (op->GetId ());
  op->SetHeight (ctx.Height () + blocksBusy);
  op->SetCharacterId (ch->GetId ());
  op->MutableProto ().mutable_armour_repair ();
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
  const proto::RevEngData* revEngData;

protected:

  bool
  IsSupported (const Building& b) const override
  {
    return cfg.Building (b.GetType ())
        .offered_services ().reverse_engineering ();
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

};

RevEngOperation::RevEngOperation (Account& a, BuildingsTable::Handle b,
                                  const std::string& t,
                                  const Inventory::QuantityT n,
                                  const ContextRefs& refs)
  : ServiceOperation(a, std::move (b), refs),
    type(t), num(n)
{
  const auto* itemData = cfg.ItemOrNull (type);
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

/* ************************************************************************** */

/**
 * A blueprint copying operation.
 */
class BlueprintCopyOperation : public ServiceOperation
{

private:

  /** The type of blueprint being copied (the original).  */
  const std::string original;

  /** The number of copies to make.  */
  const Inventory::QuantityT num;

  /**
   * The type of copies being produced.  This may be the empty string if the
   * data is invalid, e.g. the original type is no valid blueprint item.
   */
  std::string copy;

  /** The base item's complexity.  */
  unsigned complexity;

protected:

  bool
  IsSupported (const Building& b) const override
  {
    return cfg.Building (b.GetType ()).offered_services ().blueprint_copy ();
  }

  Amount
  GetBaseCost () const override
  {
    const Amount one = ctx.Params ().BlueprintCopyCost (complexity);
    return Inventory::Product (num, one);
  }

  bool IsValid () const override;
  Json::Value SpecificToPendingJson () const override;
  void ExecuteSpecific (xaya::Random& rnd) override;

public:

  explicit BlueprintCopyOperation (Account& a, BuildingsTable::Handle b,
                                   const std::string& o,
                                   const Inventory::QuantityT n,
                                   const ContextRefs& refs);

};

BlueprintCopyOperation::BlueprintCopyOperation (
      Account& a, BuildingsTable::Handle b,
      const std::string& o, const Inventory::QuantityT n,
      const ContextRefs& refs)
  : ServiceOperation(a, std::move (b), refs),
    original(o), num(n)
{
  const auto* origData = cfg.ItemOrNull (original);
  if (origData == nullptr || !origData->has_is_blueprint ())
    {
      LOG (WARNING) << "Can't copy item type " << original;
      return;
    }

  if (!origData->is_blueprint ().original ())
    {
      LOG (WARNING) << "Can't copy non-original item " << original;
      return;
    }

  const auto& baseType = origData->is_blueprint ().for_item ();
  copy = baseType + " bpc";
  complexity = cfg.Item (baseType).complexity ();
  CHECK_GT (complexity, 0)
      << "Invalid complexity " << complexity << " for type " << baseType;
}

bool
BlueprintCopyOperation::IsValid () const
{
  if (copy.empty ())
    return false;

  if (num <= 0)
    return false;

  const auto buildingId = GetBuilding ().GetId ();
  const auto& name = GetAccount ().GetName ();

  const auto inv = invTable.Get (buildingId, name);
  const auto balance = inv->GetInventory ().GetFungibleCount (original);
  if (balance == 0)
    {
      LOG (WARNING)
          << "Can't copy blueprint " << original
          << " as " << name
          << " does not own any in building " << buildingId;
      return false;
    }
  CHECK_GT (balance, 0);

  return true;
}

Json::Value
BlueprintCopyOperation::SpecificToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["type"] = "bpcopy";
  res["original"] = original;

  Json::Value outp(Json::objectValue);
  outp[copy] = IntToJson (num);
  res["output"] = outp;

  return res;
}

void
BlueprintCopyOperation::ExecuteSpecific (xaya::Random& rnd)
{
  const auto buildingId = GetBuilding ().GetId ();
  const auto& name = GetAccount ().GetName ();

  LOG (INFO)
      << name << " in building " << buildingId
      << " copies " << original << " " << num << " times";

  auto invHandle = invTable.Get (buildingId, name);
  auto& inv = invHandle->GetInventory ();
  inv.AddFungibleCount (original, -1);

  auto op = CreateOngoing ();
  const unsigned baseDuration = ctx.Params ().BlueprintCopyBlocks (complexity);
  op->SetHeight (ctx.Height () + num * baseDuration);
  op->SetBuildingId (buildingId);
  auto& cp = *op->MutableProto ().mutable_blueprint_copy ();
  cp.set_account (name);
  cp.set_original_type (original);
  cp.set_copy_type (copy);
  cp.set_num_copies (num);
}

/* ************************************************************************** */

/**
 * A general construction operation.  This can be a vehicle or fitment;
 * both work the same, with the only difference being the building service
 * that's needed (construction facility vs vehicle bay).
 */
class ConstructionOperation : public ServiceOperation
{

private:

  /** The type of blueprint being used for construction.  */
  const std::string blueprint;

  /** The number of items to construct.  */
  const Inventory::QuantityT num;

  /**
   * The output item's config data.  May be null if the operation is
   * invalid.
   */
  const proto::ItemData* outputData;

  /** The name of the output item.  */
  std::string output;

  /** Whether or not this is copying an original blueprint.  */
  bool fromOriginal;

protected:

  bool IsSupported (const Building& b) const override;

  Amount
  GetBaseCost () const override
  {
    const Amount one
        = ctx.Params ().ConstructionCost (outputData->complexity ());
    return Inventory::Product (num, one);
  }

  bool IsValid () const override;
  Json::Value SpecificToPendingJson () const override;
  void ExecuteSpecific (xaya::Random& rnd) override;

public:

  explicit ConstructionOperation (Account& a, BuildingsTable::Handle b,
                                  const std::string& bp,
                                  const Inventory::QuantityT n,
                                  const ContextRefs& refs);

};

ConstructionOperation::ConstructionOperation (
      Account& a, BuildingsTable::Handle b,
      const std::string& bp, const Inventory::QuantityT n,
      const ContextRefs& refs)
  : ServiceOperation(a, std::move (b), refs),
    blueprint(bp), num(n)
{
  const auto* bpData = cfg.ItemOrNull (blueprint);
  if (bpData == nullptr || !bpData->has_is_blueprint ())
    {
      LOG (WARNING) << "Can't construct from item type " << blueprint;
      outputData = nullptr;
      return;
    }

  fromOriginal = bpData->is_blueprint ().original ();
  output = bpData->is_blueprint ().for_item ();
  outputData = &(cfg.Item (output));
  CHECK_GT (outputData->complexity (), 0)
      << "Invalid complexity " << outputData->complexity ()
      << " for type " << output;
}

bool
ConstructionOperation::IsSupported (const Building& b) const
{
  const auto& offered = cfg.Building (b.GetType ()).offered_services ();

  if (outputData->has_vehicle ())
    return offered.vehicle_construction ();

  return offered.item_construction ();
}

bool
ConstructionOperation::IsValid () const
{
  if (outputData == nullptr)
    return false;

  if (num <= 0)
    return false;

  const auto buildingId = GetBuilding ().GetId ();
  const auto& name = GetAccount ().GetName ();

  const auto inv = invTable.Get (buildingId, name);
  for (const auto& entry : outputData->construction_resources ())
    {
      const auto balance = inv->GetInventory ().GetFungibleCount (entry.first);
      const auto required = Inventory::Product (num, entry.second);
      if (required > balance)
        {
          LOG (WARNING)
              << "Can't construct " << num << " " << output
              << " as " << name << " in building " << buildingId
              << " has only " << balance << " " << entry.first
              << " while the construction needs " << required;
          return false;
        }
    }

  const auto bpBalance = inv->GetInventory ().GetFungibleCount (blueprint);
  Inventory::QuantityT bpRequired;
  if (fromOriginal)
    bpRequired = 1;
  else
    bpRequired = num;
  if (bpRequired > bpBalance)
    {
      LOG (WARNING)
          << "Can't construct " << num << " items from " << blueprint
          << " as " << name << " in building " << buildingId
          << " has only " << bpBalance;
      return false;
    }

  return true;
}

Json::Value
ConstructionOperation::SpecificToPendingJson () const
{
  Json::Value res(Json::objectValue);
  res["type"] = "construct";
  res["blueprint"] = blueprint;

  Json::Value outp(Json::objectValue);
  outp[output] = IntToJson (num);
  res["output"] = outp;

  return res;
}

void
ConstructionOperation::ExecuteSpecific (xaya::Random& rnd)
{
  const auto buildingId = GetBuilding ().GetId ();
  const auto& name = GetAccount ().GetName ();

  LOG (INFO)
      << name << " in building " << buildingId
      << " constructs " << num << " " << output;

  auto invHandle = invTable.Get (buildingId, name);
  auto& inv = invHandle->GetInventory ();
  for (const auto& entry : outputData->construction_resources ())
    {
      const auto required = Inventory::Product (num, entry.second);
      inv.AddFungibleCount (entry.first, -required);
    }

  if (fromOriginal)
    inv.AddFungibleCount (blueprint, -1);
  else
    inv.AddFungibleCount (blueprint, -num);

  auto op = CreateOngoing ();
  op->SetBuildingId (buildingId);

  /* When constructing from an original, the items have to be constructed
     in series.  With blueprint copies, we need to have as many copies as items
     anyway, and can construct the items in parallel.  */
  const unsigned baseDuration
      = ctx.Params ().ConstructionBlocks (outputData->complexity ());
  if (fromOriginal)
    op->SetHeight (ctx.Height () + num * baseDuration);
  else
    op->SetHeight (ctx.Height () + baseDuration);

  auto& c = *op->MutableProto ().mutable_item_construction ();
  c.set_account (name);
  c.set_output_type (output);
  c.set_num_items (num);
  if (fromOriginal)
    c.set_original_type (blueprint);
}

/* ************************************************************************** */

} // anonymous namespace

ServiceOperation::ServiceOperation (Account& a, BuildingsTable::Handle b,
                                    const ContextRefs& refs)
  : accounts(refs.accounts), ongoings(refs.ongoings),
    acc(a), building(std::move (b)),
    ctx(refs.ctx), cfg(ctx.Chain ()),
    invTable(refs.invTable), itemCounts(refs.cnt)
{}

void
ServiceOperation::GetCosts (Amount& base, Amount& fee) const
{
  base = GetBaseCost ();
  CHECK_GE (base, 0);

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

OngoingsTable::Handle
ServiceOperation::CreateOngoing ()
{
  return ongoings.CreateNew (ctx.Height ());
}

bool
ServiceOperation::IsFullyValid () const
{
  if (!IsValid ())
    {
      LOG (WARNING) << "Service operation is invalid: " << rawMove;
      return false;
    }

  if (!IsSupported (GetBuilding ()))
    {
      LOG (WARNING)
          << "Building " << GetBuilding ().GetId ()
          << " does not support service operation: " << rawMove;
      return false;
    }

  Amount base, fee;
  GetCosts (base, fee);
  if (base + fee > acc.GetBalance ())
    {
      LOG (WARNING)
          << "Service operation would cost " << (base + fee)
          << ", but " << acc.GetName () << " has only " << acc.GetBalance ()
          << ": " << rawMove;
      return false;
    }

  return true;
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
                         ItemCounts& cnt,
                         OngoingsTable& ong)
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
  if (b->GetProto ().foundation ())
    {
      LOG (WARNING)
          << "Service operation requested in foundation " << buildingId;
      return nullptr;
    }

  const auto& typeVal = data["t"];
  if (!typeVal.isString ())
    {
      LOG (WARNING) << "Invalid service operation (no type): " << data;
      return nullptr;
    }
  const std::string type = typeVal.asString ();

  const ContextRefs refs(ctx, accounts, inv, cnt, ong);
  std::unique_ptr<ServiceOperation> op;
  if (type == "ref")
    op = ParseItemAmount<RefiningOperation> (acc, std::move (b), data, refs);
  else if (type == "fix")
    op = RepairOperation::Parse (acc, std::move (b), data, refs, characters);
  else if (type == "rve")
    op = ParseItemAmount<RevEngOperation> (acc, std::move (b), data, refs);
  else if (type == "cp")
    op = ParseItemAmount<BlueprintCopyOperation> (acc, std::move (b),
                                                  data, refs);
  else if (type == "bld")
    op = ParseItemAmount<ConstructionOperation> (acc, std::move (b),
                                                 data, refs);
  else
    {
      LOG (WARNING) << "Unknown service operation: " << type;
      return nullptr;
    }

  if (op == nullptr)
    {
      LOG (WARNING) << "Failed to parse service operation: " << data;
      return nullptr;
    }

  op->rawMove = data;
  return op;
}

/* ************************************************************************** */

} // namespace pxd
