/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#include "moveprocessor.hpp"

#include "buildings.hpp"
#include "burnsale.hpp"
#include "fitments.hpp"
#include "forks.hpp"
#include "jsonutils.hpp"
#include "mining.hpp"
#include "movement.hpp"
#include "prospecting.hpp"
#include "protoutils.hpp"
#include "spawn.hpp"

#include "database/faction.hpp"
#include "proto/character.pb.h"
#include "proto/roconfig.hpp"

#include <xayautil/jsonutils.hpp>

#include <sstream>

namespace pxd
{

/**
 * The maximum allowed service fee in a building.  The value is consensus
 * relevant, as it affects move validation.  It is chosen to disallow
 * "completely scam" buildings.
 */
static constexpr unsigned MAX_SERVICE_FEE_PERCENT = 1'000;

/**
 * The maximum allowed DEX fee an owner can configure for a building in
 * basis points.  Any move setting a higher fee is invalid.
 */
static constexpr unsigned MAX_DEX_FEE_BPS = 3'000;

/** Airdrop of vCHI for each new character during testing.  */
static constexpr Amount VCHI_AIRDROP = 1'000;

/* ************************************************************************** */

BaseMoveProcessor::BaseMoveProcessor (Database& d, DynObstacles& o,
                                      const Context& c)
  : ctx(c), db(d), dyn(o),
    accounts(db), buildings(db), characters(db),
    groundLoot(db), buildingInv(db),
    orders(db), dexHistory(db),
    itemCounts(db), moneySupply(db),
    ongoings(db), regions(db, ctx.Height ())
{}

bool
BaseMoveProcessor::ExtractMoveBasics (const Json::Value& moveObj,
                                      std::string& name, Json::Value& mv,
                                      Amount& paidToDev,
                                      Amount& burnt) const
{
  VLOG (1) << "Processing move:\n" << moveObj;
  CHECK (moveObj.isObject ());

  CHECK (moveObj.isMember ("move"));
  mv = moveObj["move"];
  if (!mv.isObject ())
    {
      LOG (WARNING) << "Move is not an object: " << mv;
      return false;
    }

  const auto& nameVal = moveObj["name"];
  CHECK (nameVal.isString ());
  name = nameVal.asString ();

  paidToDev = 0;
  burnt = 0;

  const auto& outVal = moveObj["out"];
  if (outVal.isObject ())
    {
      const auto& devAddr = ctx.RoConfig ()->params ().dev_addr ();
      if (outVal.isMember (devAddr))
        CHECK (xaya::ChiAmountFromJson (outVal[devAddr], paidToDev));

      const auto& burnAddr = ctx.RoConfig ()->params ().burn_addr ();
      if (outVal.isMember (burnAddr))
        CHECK (xaya::ChiAmountFromJson (outVal[burnAddr], burnt));
    }

  return true;
}

void
BaseMoveProcessor::TryCharacterCreation (const std::string& name,
                                         const Json::Value& mv,
                                         Amount& paidToDev)
{
  const auto& cmd = mv["nc"];
  if (!cmd.isArray ())
    return;

  VLOG (1) << "Attempting to create new characters through move: " << cmd;

  const auto account = accounts.GetByName (name);
  CHECK (account != nullptr && account->IsInitialised ());
  const Faction faction = account->GetFaction ();
  VLOG (1)
      << "The new characters' account " << name
      << " has faction: " << FactionToString (faction);

  for (const auto& cur : cmd)
    {
      if (!cur.isObject ())
        {
          LOG (WARNING)
              << "Character creation entry is not an object: " << cur;
          continue;
        }

      if (cur.size () != 0)
        {
          LOG (WARNING) << "Character creation has extra fields: " << cur;
          continue;
        }

      VLOG (1) << "Trying to create character, amount paid left: " << paidToDev;
      const Amount cost = ctx.RoConfig ()->params ().character_cost () * COIN;
      if (paidToDev < cost)
        {
          /* In this case, we can return rather than continue with the next
             iteration.  If all money paid is "used up" already, then it won't
             be enough for later entries of the array, either.  */
          LOG (WARNING)
              << "Required amount for new character not paid by " << name
              << " (only have " << paidToDev << ")";
          return;
        }

      if (characters.CountForOwner (name)
            >= ctx.RoConfig ()->params ().character_limit ())
        {
          LOG (WARNING)
              << "Account " << name << " has the maximum number of characters"
              << " already, can't create another one";
          return;
        }

      PerformCharacterCreation (*account, faction);
      paidToDev -= cost;
      VLOG (1) << "After character creation, paid to dev left: " << paidToDev;
    }
}

void
BaseMoveProcessor::TryCharacterUpdates (const std::string& name,
                                        const Json::Value& mv)
{
  const auto& cmd = mv["c"];

  /* The character update can either be an array of individual operations,
     or just a single object for a single operation.  */
  Json::Value ops;
  if (cmd.isArray ())
    ops = cmd;
  else if (cmd.isObject ())
    {
      ops = Json::Value(Json::arrayValue);
      ops.append (cmd);
    }
  else
    return;

  for (const auto& op : ops)
    {
      if (!op.isObject ())
        {
          LOG (WARNING) << "Character update entry is not an object: " << op;
          continue;
        }

      /* Also the ID in the update itself can either be a single ID or
         an array of IDs, which will be batch-updated with the same
         operation.  */
      const auto& idOrIds = op["id"];
      Json::Value ids;
      if (idOrIds.isNull ())
        {
          LOG (WARNING) << "Missing ID in character update entry: " << op;
          continue;
        }
      else if (idOrIds.isArray ())
        ids = idOrIds;
      else
        ids.append (idOrIds);

      for (const auto& idVal : ids)
        {
          Database::IdT id;
          if (!IdFromJson (idVal, id))
            {
              LOG (WARNING)
                  << "Invalid ID in character update entry: " << idVal;
              continue;
            }

          auto c = characters.GetById (id);
          if (c == nullptr)
            {
              LOG (WARNING) << "Character ID does not exist: " << id;
              continue;
            }

          if (c->GetOwner () != name)
            {
              LOG (WARNING)
                  << "User " << name
                  << " is not allowed to update character owned by "
                  << c->GetOwner ();
              continue;
            }

          PerformCharacterUpdate (*c, op);
        }
    }
}

namespace
{

/**
 * Tries to extract a service-fee update for a building into
 * the new configuration proto.
 */
bool
MaybeUpdateServiceFee (const Json::Value& upd, proto::Building::Config& cfg)
{
  if (!xaya::IsIntegerValue (upd) || !upd.isUInt64 ())
    return false;

  const uint64_t val = upd.asUInt64 ();
  if (val > MAX_SERVICE_FEE_PERCENT)
    {
      LOG (WARNING) << "Service fee " << val << "% is too much";
      return false;
    }

  cfg.set_service_fee_percent (val);
  return true;
}

/**
 * Tries to extract a DEX-fee update in a building into
 * the new configuration proto.
 */
bool
MaybeUpdateDexFee (const Json::Value& upd, proto::Building::Config& cfg)
{
  if (!xaya::IsIntegerValue (upd) || !upd.isUInt64 ())
    return false;

  const uint64_t val = upd.asUInt64 ();
  if (val > MAX_DEX_FEE_BPS)
    {
      LOG (WARNING) << "DEX fee of " << val << " basis points is too";
      return false;
    }

  cfg.set_dex_fee_bps (val);
  return true;
}

} // anonymous namespace

void
BaseMoveProcessor::MaybeTransferBuilding (Building& b, const Json::Value& upd)
{
  CHECK (upd.isObject ());
  const auto& sendToVal = upd["send"];
  if (!sendToVal.isString ())
    return;
  const std::string sendTo = sendToVal.asString ();

  const auto a = accounts.GetByName (sendTo);
  if (a == nullptr || !a->IsInitialised ())
    {
      LOG (WARNING)
          << "Can't send building " << b.GetId ()
          << " to uninitialised account " << sendTo;
      return;
    }
  if (a->GetFaction () != b.GetFaction ())
    {
      LOG (WARNING)
          << "Can't send building " << b.GetId ()
          << " to account " << sendTo << " of different faction";
      return;
    }

  PerformBuildingTransfer (b, *a);
}

void
BaseMoveProcessor::TryBuildingUpdate (Building& b, const Json::Value& upd)
{
  proto::Building::Config newConfig;

  bool updated = false;
  if (MaybeUpdateServiceFee (upd["sf"], newConfig))
    updated = true;
  if (MaybeUpdateDexFee (upd["xf"], newConfig))
    updated = true;

  if (updated)
    PerformBuildingConfigUpdate (b, newConfig);

  MaybeTransferBuilding (b, upd);
}

void
BaseMoveProcessor::TryBuildingUpdates (const std::string& name,
                                       const Json::Value& mv)
{
  const auto& cmd = mv["b"];

  /* The building update can either be an array of individual operations,
     or just a single object for a single operation.  */
  Json::Value ops;
  if (cmd.isArray ())
    ops = cmd;
  else if (cmd.isObject ())
    {
      ops = Json::Value(Json::arrayValue);
      ops.append (cmd);
    }
  else
    return;

  for (const auto& op : ops)
    {
      if (!op.isObject ())
        {
          LOG (WARNING)
              << "Building update entry is not an object: " << op;
          continue;
        }

      Database::IdT id;
      if (!IdFromJson (op["id"], id))
        {
          LOG (WARNING)
              << "Invalid ID in building update entry: " << op;
          continue;
        }

      auto b = buildings.GetById (id);
      if (b == nullptr)
        {
          LOG (WARNING) << "Building ID does not exist: " << id;
          continue;
        }

      if (b->GetFaction () == Faction::ANCIENT)
        {
          LOG (WARNING)
              << "User " << name
              << " is not allowed to update ancient buildings";
          continue;
        }

      if (b->GetOwner () != name)
        {
          LOG (WARNING)
              << "User " << name
              << " is not allowed to update building owned by "
              << b->GetOwner ();
          continue;
        }

      TryBuildingUpdate (*b, op);
    }
}

void
BaseMoveProcessor::TryMobileRefining (Character& c, const Json::Value& upd)
{
  CHECK (upd.isObject ());
  const auto& ref = upd["ref"];
  if (ref.isNull ())
    return;

  auto a = accounts.GetByName (c.GetOwner ());
  CHECK (a != nullptr);

  auto op = ServiceOperation::ParseMobileRefining (*a, c, ref, ctx,
                                                   accounts, buildingInv,
                                                   itemCounts, ongoings);
  if (op != nullptr && op->IsFullyValid ())
    PerformServiceOperation (*op);
}

void
BaseMoveProcessor::TryServiceOperations (const std::string& name,
                                         const Json::Value& mv)
{
  const auto& cmds = mv["s"];
  if (!cmds.isArray ())
    return;

  const auto a = accounts.GetByName (name);
  CHECK (a != nullptr);

  for (const auto& op : cmds)
    {
      auto parsed = ServiceOperation::Parse (*a, op, ctx,
                                             accounts,
                                             buildings, buildingInv,
                                             characters, itemCounts,
                                             ongoings);
      if (parsed != nullptr && parsed->IsFullyValid ())
        PerformServiceOperation (*parsed);
    }
}

void
BaseMoveProcessor::TryDexOperations (const std::string& name,
                                     const Json::Value& mv)
{
  const auto& cmds = mv["x"];
  if (!cmds.isArray ())
    return;

  const auto a = accounts.GetByName (name);
  CHECK (a != nullptr);

  for (const auto& op : cmds)
    {
      auto parsed = DexOperation::Parse (*a, op, ctx,
                                         accounts,
                                         buildings, buildingInv,
                                         orders, dexHistory);
      if (parsed == nullptr)
        {
          LOG (WARNING) << "Malformed DEX operation:\n" << op;
          continue;
        }
      if (parsed->IsValid ())
        PerformDexOperation (*parsed);
    }
}

bool
BaseMoveProcessor::ParseCoinTransferBurn (const Account& a,
                                          const Json::Value& moveObj,
                                          CoinTransferBurn& op,
                                          Amount& burntChi)
{
  CHECK (moveObj.isObject ());
  const auto& cmd = moveObj["vc"];
  if (!cmd.isObject ())
    return false;

  Amount balance = a.GetBalance ();
  Amount total = 0;

  const auto& mint = cmd["m"];
  if (mint.isObject () && mint.empty ())
    {
      const Amount soldBefore = moneySupply.Get ("burnsale");
      op.minted = ComputeBurnsaleAmount (burntChi, soldBefore, ctx);
      balance += op.minted;
    }
  else
    {
      op.minted = 0;
      LOG_IF (WARNING, !mint.isNull ()) << "Invalid mint command: " << mint;
    }

  const auto& burn = cmd["b"];
  if (CoinAmountFromJson (burn, op.burnt))
    {
      if (total + op.burnt <= balance)
        total += op.burnt;
      else
        {
          LOG (WARNING)
              << a.GetName () << " has only a balance of " << balance
              << ", can't burn " << op.burnt << " coins";
          op.burnt = 0;
        }
    }
  else
    op.burnt = 0;

  const auto& transfers = cmd["t"];
  if (transfers.isObject ())
    {
      op.transfers.clear ();
      for (auto it = transfers.begin (); it != transfers.end (); ++it)
        {
          CHECK (it.key ().isString ());
          const std::string to = it.key ().asString ();

          Amount amount;
          if (!CoinAmountFromJson (*it, amount))
            {
              LOG (WARNING)
                  << "Invalid coin transfer from " << a.GetName ()
                  << " to " << to << ": " << *it;
              continue;
            }

          if (total + amount > balance)
            {
              LOG (WARNING)
                  << "Transfer of " << amount << " from " << a.GetName ()
                  << " to " << to << " would exceed the balance";
              continue;
            }

          /* Self transfers are a no-op, so we just ignore them (and do not
             update the total sent, so the balance is still available for
             future transfers).  */
          if (to == a.GetName ())
            continue;

          total += amount;
          CHECK (op.transfers.emplace (to, amount).second);
        }
    }

  CHECK_LE (total, balance);
  return total > 0 || op.minted > 0;
}

bool
BaseMoveProcessor::ParseCharacterWaypoints (const Character& c,
                                            const Json::Value& upd,
                                            std::vector<HexCoord>& wp)
{
  CHECK (upd.isObject ());
  if (!upd.isMember ("wp"))
    return false;

  const auto& wpVal = upd["wp"];

  /* Special case:  If an explicit null is set, we interpret that as
     empty waypoints, i.e. stop movement.  */
  if (wpVal.isNull ())
    {
      wp.clear ();
      return true;
    }

  if (!wpVal.isString ())
    {
      LOG (WARNING) << "Expected string for encoded waypoints: " << upd;
      return false;
    }

  if (c.IsBusy ())
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't set waypoints";
      return false;
    }

  if (c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is inside a building, can't set waypoints";
      return false;
    }

  if (!DecodeWaypoints (wpVal.asString (), wp))
    {
      LOG (WARNING)
          << "Invalid waypoints given for character " << c.GetId ()
          << ", not updating movement";
      return false;
    }

  return true;
}

bool
BaseMoveProcessor::ParseCharacterWaypointExtension (const Character& c,
                                                    const Json::Value& upd,
                                                    const bool pendingWp,
                                                    std::vector<HexCoord>& wp)
{
  CHECK (upd.isObject ());
  const auto& wpx = upd["wpx"];
  if (!wpx.isString ())
    return false;

  /* A waypoint extension is only valid if the character is currently moving,
     i.e. has already some waypoints set.  This ensures that we will not
     violate any assumptions like "not moving and mining at the same time",
     without having to explicitly enforce them explicitly by changing
     the character state.  */
  const auto& pb = c.GetProto ();
  if (!pendingWp && pb.movement ().waypoints ().empty ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is not moving, can't extend waypoints";
      return false;
    }

  if (!DecodeWaypoints (wpx.asString (), wp))
    {
      LOG (WARNING)
          << "Invalid waypoints given for character " << c.GetId ()
          << ", not extending movement";
      return false;
    }

  return true;
}

bool
BaseMoveProcessor::ParseEnterBuilding (const Character& c,
                                       const Json::Value& upd,
                                       Database::IdT& buildingId)
{
  CHECK (upd.isObject ());
  if (!upd.isMember ("eb"))
    return false;

  if (c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is in building, can't enter another one";
      return false;
    }

  const auto& val = upd["eb"];

  /* null value means to cancel any entering.  */
  if (val.isNull ())
    {
      VLOG (1)
          << "Character " << c.GetId ()
          << " no longer wants to enter a building";
      buildingId = Database::EMPTY_ID;
      return true;
    }

  /* Otherwise, see if this is a valid building ID.  */
  if (!IdFromJson (val, buildingId))
    {
      LOG (WARNING) << "Not a building ID: " << val;
      return false;
    }

  auto b = buildings.GetById (buildingId);
  if (b == nullptr)
    {
      LOG (WARNING) << "Building does not exist: " << val;
      return false;
    }

  /* Everyone can enter ancient buildings, but otherwise characters can only
     enter buildings of their own faction.  */
  if (b->GetFaction () != Faction::ANCIENT
        && b->GetFaction () != c.GetFaction ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " can't enter building " << buildingId
          << " of different faction";
      return false;
    }

  VLOG (1)
      << "Character " << c.GetId ()
      << " wants to enter building " << buildingId;

  return true;
}

bool
BaseMoveProcessor::ParseExitBuilding (const Character& c,
                                      const Json::Value& upd)
{
  CHECK (upd.isObject ());
  const auto& val = upd["xb"];
  if (!val.isObject ())
    return false;

  if (val.size () != 0)
    {
      LOG (WARNING) << "Invalid exit-building move: " << upd;
      return false;
    }

  if (c.IsBusy ())
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't exit building";
      return false;
    }

  if (!c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is not in building and can't exit";
      return false;
    }

  VLOG (1)
      << "Character " << c.GetId ()
      << " will exit building " << c.GetBuildingId ();

  return true;
}

namespace
{

/**
 * Parses a JSON dictionary giving fungible items and their quantities
 * into a std::map.  This will contain all item names and quantities
 * for valid entries.
 */
FungibleAmountMap
ParseFungibleQuantities (const Context& ctx, const Json::Value& obj)
{
  CHECK (obj.isObject ());

  FungibleAmountMap res;
  for (auto it = obj.begin (); it != obj.end (); ++it)
    {
      const auto& keyVal = it.key ();
      CHECK (keyVal.isString ());
      const std::string key = keyVal.asString ();

      if (ctx.RoConfig ().ItemOrNull (key) == nullptr)
        {
          LOG (WARNING) << "Invalid fungible item: " << key;
          continue;
        }

      Quantity cnt;
      if (!QuantityFromJson (*it, cnt))
        {
          LOG (WARNING)
              << "Invalid fungible amount for item " << key << ": " << *it;
          continue;
        }

      const auto ins = res.emplace (key, cnt);
      CHECK (ins.second) << "Duplicate key: " << key;
    }

  return res;
}

} // anonymous namespace

FungibleAmountMap
BaseMoveProcessor::ParseDropPickupFungible (const Json::Value& cmd) const
{
  if (!cmd.isObject ())
    return {};

  const auto& fungible = cmd["f"];
  if (!fungible.isObject ())
    {
      LOG (WARNING) << "No fungible object entry in command: " << cmd;
      return {};
    }
  if (cmd.size () != 1)
    {
      LOG (WARNING) << "Extra fields in command: " << cmd;
      return {};
    }

  return ParseFungibleQuantities (ctx, fungible);
}

bool
BaseMoveProcessor::ParseCharacterProspecting (const Character& c,
                                              const Json::Value& upd,
                                              Database::IdT& regionId)
{
  CHECK (upd.isObject ());
  const auto& cmd = upd["prospect"];
  if (!cmd.isObject ())
    return false;

  if (!cmd.empty ())
    {
      LOG (WARNING)
          << "Invalid prospecting command for character " << c.GetId ()
          << ": " << cmd;
      return false;
    }

  if (!c.GetProto ().has_prospecting_blocks ())
    {
      LOG (WARNING) << "Character " << c.GetId () << " cannot prospect";
      return false;
    }

  if (c.IsBusy ())
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't prospect";
      return false;
    }

  if (c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is inside a building, can't prospect";
      return false;
    }

  const auto& pos = c.GetPosition ();
  regionId = ctx.Map ().Regions ().GetRegionId (pos);
  VLOG (1)
      << "Character " << c.GetId ()
      << " is trying to prospect region " << regionId;

  return CanProspectRegion (c, *regions.GetById (regionId), ctx);
}

bool
BaseMoveProcessor::ParseCharacterMining (const Character& c,
                                         const Json::Value& upd,
                                         Database::IdT& regionId)
{
  CHECK (upd.isObject ());
  const auto& cmd = upd["mine"];
  if (!cmd.isObject ())
    return false;

  if (!cmd.empty ())
    {
      LOG (WARNING)
          << "Invalid mining command for character " << c.GetId ()
          << ": " << cmd;
      return false;
    }

  if (!c.GetProto ().has_mining ())
    {
      LOG (WARNING) << "Character " << c.GetId () << " can't mine";
      return false;
    }

  if (c.IsBusy ())
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't mine";
      return false;
    }

  if (c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is inside a building, can't mine";
      return false;
    }

  const auto& pos = c.GetPosition ();
  regionId = ctx.Map ().Regions ().GetRegionId (pos);
  VLOG (1)
      << "Character " << c.GetId ()
      << " wants to start mining region " << regionId;

  if (c.GetProto ().has_movement ())
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " can't mine while it is moving";
      return false;
    }

  auto r = regions.GetById (regionId);
  const auto& pbRegion = r->GetProto ();
  if (!pbRegion.has_prospection ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " can't mine in region " << regionId << " which is not prospected";
      return false;
    }

  const auto left = r->GetResourceLeft ();
  if (left == 0)
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " can't mine in region " << regionId
          << " which has no resource left";
      return false;
    }
  CHECK_GT (left, 0);

  return true;
}

namespace
{

/**
 * Checks if the given character has a fully repaired and shield regenerated
 * vehicle.  That's a condition before changing fitments or vehicle.
 */
bool
HasFullHp (const Character& c)
{
  const auto& hp = c.GetHP ();
  const auto& maxHp = c.GetRegenData ().max_hp ();

  if (hp.armour () < maxHp.armour ())
    return false;
  if (hp.shield () < maxHp.shield ())
    return false;

  CHECK_EQ (hp.armour (), maxHp.armour ());
  CHECK_EQ (hp.shield (), maxHp.shield ());
  return true;
}

} // anonymous namespace

bool
BaseMoveProcessor::ParseChangeVehicle (const Character& c,
                                       const Json::Value& upd,
                                       std::string& vehicle)
{
  CHECK (upd.isObject ());
  const auto& cmd = upd["v"];
  if (!cmd.isString ())
    return false;
  vehicle = cmd.asString ();

  if (!HasFullHp (c))
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " can't change vehicles without full HP";
      return false;
    }

  if (!c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is not in building and can't change vehicles";
      return false;
    }
  const auto buildingId = c.GetBuildingId ();

  /* In theory, changing a vehicle inside a foundation is not possible
     anyway because there can be no account inventory there.  But it probably
     does not hurt to explicitly enforce this in any case.  */
  auto b = buildings.GetById (buildingId);
  if (b->GetProto ().foundation ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " cannot change vehicle inside a foundation only";
      return false;
    }

  const auto* data = ctx.RoConfig ().ItemOrNull (vehicle);
  if (data == nullptr || !data->has_vehicle ())
    {
      LOG (WARNING) << "Invalid vehicle: " << vehicle;
      return false;
    }

  auto inv = buildingInv.Get (buildingId, c.GetOwner ());
  if (inv->GetInventory ().GetFungibleCount (vehicle) == 0)
    {
      LOG (WARNING)
          << "User " << c.GetOwner () << " does not own " << vehicle
          << " in building " << buildingId;
      return false;
    }

  return true;
}

bool
BaseMoveProcessor::ParseSetFitments (const Character& c, const Json::Value& upd,
                                     std::vector<std::string>& fitments)
{
  CHECK (upd.isObject ());
  const auto& cmd = upd["fit"];
  if (!cmd.isArray ())
    return false;

  if (!HasFullHp (c))
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " can't change fitments without full HP";
      return false;
    }

  if (!c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is not in building and can't change fitments";
      return false;
    }
  const auto buildingId = c.GetBuildingId ();

  auto b = buildings.GetById (buildingId);
  if (b->GetProto ().foundation ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " cannot change fitments inside a foundation only";
      return false;
    }

  fitments.clear ();
  for (const auto& f : cmd)
    {
      if (!f.isString ())
        {
          LOG (WARNING) << "Fitment entry is not a string: " << f;
          return false;
        }
      const auto item = f.asString ();

      const auto* data = ctx.RoConfig ().ItemOrNull (item);
      if (data == nullptr || !data->has_fitment ())
        {
          LOG (WARNING) << "Invalid fitment: " << item;
          return false;
        }

      fitments.push_back (item);
    }

  /* Make sure the user has the required items in their inventory.  Existing
     fitments from the character are also fine, as they will be removed
     before being added back.  */
  std::unordered_map<std::string, Quantity> items;
  for (const auto& f : fitments)
    ++items[f];
  for (const auto& f : c.GetProto ().fitments ())
    --items[f];
  auto inv = buildingInv.Get (buildingId, c.GetOwner ());
  for (const auto& entry : items)
    if (entry.second > inv->GetInventory ().GetFungibleCount (entry.first))
      {
        LOG (WARNING)
            << "Fitment items are not available to user " << c.GetOwner ()
            << " in building " << buildingId << ":\n" << cmd;
        return false;
      }

  if (!CheckVehicleFitments (c.GetProto ().vehicle (), fitments, ctx))
    {
      LOG (WARNING)
          << "Fitments for character " << c.GetId ()
          << " on vehicle type " << c.GetProto ().vehicle ()
          << " are not possible:\n" << cmd;
      return false;
    }

  return true;
}

namespace
{

/**
 * Parses a building configuration (type and shape trafo) from
 * JSON.  This is shared between proper "found building" moves
 * and god-mode build commands.
 */
bool
ParseBuildingConfig (const Context& ctx, const Json::Value& build,
                     std::string& type, proto::ShapeTransformation& trafo)
{
  CHECK (build.isObject ());

  auto val = build["t"];
  if (!val.isString ())
    {
      LOG (WARNING) << "Building element has invalid type: " << build;
      return false;
    }
  type = val.asString ();

  if (ctx.RoConfig ().BuildingOrNull (type) == nullptr)
    {
      LOG (WARNING) << "Invalid type for building: " << type;
      return false;
    }

  val = build["rot"];
  if (!xaya::IsIntegerValue (val) || !val.isUInt () || val.asUInt () > 5)
    {
      LOG (WARNING) << "Building element has invalid rotation: " << build;
      return false;
    }
  const unsigned rot = val.asUInt ();

  trafo.Clear ();
  trafo.set_rotation_steps (rot);

  return true;
}

} // anonymous namespace

bool
BaseMoveProcessor::ParseFoundBuilding (const Character& c,
                                       const Json::Value& upd,
                                       std::string& type,
                                       proto::ShapeTransformation& trafo)
{
  CHECK (upd.isObject ());
  const auto& build = upd["fb"];
  if (!build.isObject ())
    return false;

  if (build.size () != 2 || !ParseBuildingConfig (ctx, build, type, trafo))
    {
      LOG (WARNING) << "Invalid building element: " << build;
      return false;
    }

  if (c.IsBusy ())
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't found a building";
      return false;
    }

  if (c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is inside a building, can't found a building";
      return false;
    }

  const auto& roData = ctx.RoConfig ().Building (type);
  if (!roData.has_construction ())
    {
      LOG (WARNING) << "Building " << type << " cannot be constructed";
      return false;
    }

  if (roData.construction ().has_faction ())
    {
      const auto roFaction
          = FactionFromString (roData.construction ().faction ());
      if (roFaction != c.GetFaction ())
        {
          LOG (WARNING)
              << "Building " << type
              << " cannot be constructed by " << c.GetOwner ()
              << " of faction " << FactionToString (c.GetFaction ());
          return false;
        }
    }

  const auto& inv = c.GetInventory ();
  for (const auto& entry : roData.construction ().foundation ())
    {
      const auto available = inv.GetFungibleCount (entry.first);
      if (entry.second > available)
        {
          LOG (WARNING)
              << "Character " << c.GetId ()
              << " has only " << available << " " << entry.first
              << " but needs " << entry.second
              << " to build foundations of " << type;
          return false;
        }
    }

  /* Before we check the placement, we have to temporarily remove the
     building character itself.  It is fine if they are in the way, as they
     will automatically enter the foundation once placed.  */
  MoveInDynObstacles dynMover(c, dyn);
  if (!CanPlaceBuilding (type, trafo, c.GetPosition (), dyn, ctx))
    {
      LOG (WARNING)
          << "Can't place building in this configuration at "
          << c.GetPosition ()
          << ": " << build;
      return false;
    }

  return true;
}

/* ************************************************************************** */

void
MoveProcessor::ProcessAll (const Json::Value& moveArray)
{
  CHECK (moveArray.isArray ());
  LOG (INFO) << "Processing " << moveArray.size () << " moves...";

  for (const auto& m : moveArray)
    ProcessOne (m);
}

void
MoveProcessor::ProcessAdmin (const Json::Value& admArray)
{
  CHECK (admArray.isArray ());
  LOG (INFO) << "Processing " << admArray.size () << " admin commands...";

  for (const auto& cmd : admArray)
    {
      CHECK (cmd.isObject ());
      ProcessOneAdmin (cmd["cmd"]);
    }
}

void
MoveProcessor::ProcessOneAdmin (const Json::Value& cmd)
{
  if (!cmd.isObject ())
    return;

  HandleGodMode (cmd["god"]);
}

void
MoveProcessor::ProcessOne (const Json::Value& moveObj)
{
  std::string name;
  Json::Value mv;
  Amount paidToDev, burnt;
  if (!ExtractMoveBasics (moveObj, name, mv, paidToDev, burnt))
    return;

  /* Ensure that the account database entry exists.  In other words, we
     have accounts (although perhaps uninitialised) for everyone who
     ever sent a Taurion move.  */
  if (accounts.GetByName (name) == nullptr)
    {
      LOG (INFO) << "Creating uninitialised account for " << name;
      accounts.CreateNew (name);
    }

  /* Handle coin transfers before other game operations.  They are even
     valid without a properly initialised account (so that vCHI works as
     a real cryptocurrency, not necessarily tied to the game).

     This also ensures that if funds run out, then the explicit transfers
     are done with priority over the other operations that may require coins
     implicitly.  */
  TryCoinOperation (name, mv, burnt);

  /* At this point, we terminate if the game-play itself has not started.
     This is more or less when the "game world is created", except that we
     do allow Cubit operations already from the start of the burnsale.  */
  if (!ctx.Forks ().IsActive (Fork::GameStart))
    return;

  /* Handle trading / DEX operations now.  They are independent of
     account initialisation, but only start with the game start.  */
  TryDexOperations (name, mv);

  /* We perform account updates first.  That ensures that it is possible to
     e.g. choose one's faction and create characters in a single move.  */
  TryAccountUpdate (name, mv["a"]);

  /* If there is no account (after potentially updating/initialising it),
     then let's not try to process any more updates.  This explicitly
     enforces that accounts have to be initialised before doing anything
     else, even if perhaps some changes wouldn't actually require access
     to an account in their processing.  */
  if (!accounts.GetByName (name)->IsInitialised ())
    {
      VLOG (1)
          << "Account " << name << " is not yet initialised,"
          << " ignoring parts of the move " << moveObj;
      return;
    }

  /* Note that the order between character update and character creation
     matters:  By having the update *before* the creation, we explicitly
     forbid a situation in which a newly created character is updated right
     away.  That would be tricky (since the ID would have to be predicted),
     but it would have been possible sometimes if the order were reversed.
     We want to exclude such trickery and thus do the update first.  */
  TryCharacterUpdates (name, mv);
  TryCharacterCreation (name, mv, paidToDev);

  TryBuildingUpdates (name, mv);
  TryServiceOperations (name, mv);

  /* If any burnt or paid-to-dev coins are left, it means probably something
     has gone wrong and the user overpaid due to a frontend bug.  */
  LOG_IF (WARNING, paidToDev > 0 || burnt > 0)
      << "At the end of the move, " << name
      << " has " << paidToDev << " paid-to-dev and "
      << burnt << " burnt CHI satoshi left";
}

void
MoveProcessor::PerformCharacterCreation (Account& acc, const Faction f)
{
  SpawnCharacter (acc.GetName (), f, characters, ctx);

  /* FIXME: For the full game, remove this.  */
  LOG (INFO)
      << "Airdropping " << VCHI_AIRDROP << " to " << acc.GetName ()
      << " for newly created character";
  acc.AddBalance (VCHI_AIRDROP);
}

namespace
{

/**
 * Sets the character's chosen speed from the update, if there is a command
 * to do so in it.
 */
void
MaybeSetCharacterSpeed (Character& c, const Json::Value& upd)
{
  CHECK (upd.isObject ());
  const auto& val = upd["speed"];
  if (!xaya::IsIntegerValue (val) || !val.isUInt64 ())
    return;

  if (!c.GetProto ().has_movement ())
    {
      LOG (WARNING)
          << "Can't set speed on character " << c.GetId ()
          << ", which is not moving";
      return;
    }

  const uint64_t speed = val.asUInt64 ();
  if (speed == 0 || speed > MAX_CHOSEN_SPEED)
    {
      LOG (WARNING)
          << "Invalid chosen speed for character " << c.GetId ()
          << ": " << upd;
      return;
    }

  VLOG (1)
      << "Setting chosen speed for character " << c.GetId ()
      << " to: " << speed;
  c.MutableProto ().mutable_movement ()->set_chosen_speed (speed);
}

} // anonymous namespace

void
MoveProcessor::MaybeTransferCharacter (Character& c, const Json::Value& upd)
{
  CHECK (upd.isObject ());
  const auto& sendToVal = upd["send"];
  if (!sendToVal.isString ())
    return;
  const std::string sendTo = sendToVal.asString ();

  if (characters.CountForOwner (sendTo)
        >= ctx.RoConfig ()->params ().character_limit ())
    {
      LOG (WARNING)
          << "Account " << sendTo << " already has the maximum number of"
          << " characters, can't receive character " << c.GetId ();
      return;
    }

  const auto a = accounts.GetByName (sendTo);
  if (a == nullptr || !a->IsInitialised ())
    {
      LOG (WARNING)
          << "Can't send character " << c.GetId ()
          << " to uninitialised account " << sendTo;
      return;
    }
  if (a->GetFaction () != c.GetFaction ())
    {
      LOG (WARNING)
          << "Can't send character " << c.GetId ()
          << " to account " << sendTo << " of different faction";
      return;
    }

  VLOG (1)
      << "Sending character " << c.GetId ()
      << " from " << c.GetOwner () << " to " << sendTo;
  c.SetOwner (sendTo);
}

void
MoveProcessor::MaybeSetCharacterWaypoints (Character& c, const Json::Value& upd)
{
  std::vector<HexCoord> wp;
  if (!ParseCharacterWaypoints (c, upd, wp))
    return;

  VLOG (1)
      << "Updating movement for character " << c.GetId ()
      << " from waypoints: " << upd["wp"];

  StopCharacter (c);
  StopMining (c);

  if (wp.empty ())
    return;

  /* If the character has no movement speed, then we also do not set any
     waypoints at all for it.  */
  if (c.GetProto ().speed () == 0)
    {
      LOG (WARNING)
          << "Ignoring waypoints for character " << c.GetId ()
          << " with zero speed";
      return;
    }

  auto* pb = c.MutableProto ().mutable_movement ()->mutable_waypoints ();
  pb->Clear ();
  AddRepeatedCoords (wp, *pb);
}

void
MoveProcessor::MaybeExtendCharacterWaypoints (Character& c,
                                              const Json::Value& upd)
{
  std::vector<HexCoord> wp;
  if (!ParseCharacterWaypointExtension (c, upd, false, wp))
    return;

  VLOG (1)
      << "Extending waypoints of character " << c.GetId ()
      << " by: " << upd["wpx"];

  auto* pb = c.MutableProto ().mutable_movement ()->mutable_waypoints ();
  AddRepeatedCoords (wp, *pb);
}

void
MoveProcessor::MaybeEnterBuilding (Character& c, const Json::Value& upd)
{
  Database::IdT buildingId;
  if (!ParseEnterBuilding (c, upd, buildingId))
    return;

  c.SetEnterBuilding (buildingId);
}

void
MoveProcessor::MaybeExitBuilding (Character& c, const Json::Value& upd)
{
  if (!ParseExitBuilding (c, upd))
    return;

  LeaveBuilding (buildings, c, rnd, dyn, ctx);
}

void
MoveProcessor::MaybeStartProspecting (Character& c, const Json::Value& upd)
{
  Database::IdT regionId;
  if (!ParseCharacterProspecting (c, upd, regionId))
    return;

  auto r = regions.GetById (regionId);
  r->MutableProto ().set_prospecting_character (c.GetId ());

  /* If the region was already prospected and is now being reprospected,
     remove the old result.  */
  r->MutableProto ().clear_prospection ();

  StopCharacter (c);

  const unsigned blocks = c.GetProto ().prospecting_blocks ();
  CHECK_GT (blocks, 0);

  auto op = ongoings.CreateNew (ctx.Height ());
  c.MutableProto ().set_ongoing (op->GetId ());
  op->SetHeight (ctx.Height () + blocks);
  op->SetCharacterId (c.GetId ());
  op->MutableProto ().mutable_prospection ();
}

void
MoveProcessor::MaybeStartMining (Character& c, const Json::Value& upd)
{
  Database::IdT regionId;
  if (!ParseCharacterMining (c, upd, regionId))
    return;

  VLOG (1)
      << "Starting to mine with character " << c.GetId ()
      << " in region " << regionId;
  c.MutableProto ().mutable_mining ()->set_active (true);
}

namespace
{

/**
 * Drops all inventory of a character (which is assumed to be inside a building)
 * into the owner's inventory in the building.  This happens automatically
 * when changing vehicle or fitments.
 */
void
DropAllInventory (Character& c, BuildingInventory& inv)
{
  CHECK_EQ (c.GetBuildingId (), inv.GetBuildingId ());
  CHECK_EQ (c.GetOwner (), inv.GetAccount ());

  inv.GetInventory () += c.GetInventory ();
  c.GetInventory ().Clear ();
}

/**
 * Removes all fitments of the given character.
 */
void
RemoveAllFitments (Character& c, BuildingInventory& inv)
{
  auto& pb = c.MutableProto ();
  for (const auto& f : pb.fitments ())
    inv.GetInventory ().AddFungibleCount (f, 1);
  pb.clear_fitments ();
}

} // anonymous namespace

void
MoveProcessor::MaybeChangeVehicle (Character& c, const Json::Value& upd)
{
  std::string vehicle;
  if (!ParseChangeVehicle (c, upd, vehicle))
    return;

  VLOG (1)
      << "Changing vehicle of character " << c.GetId ()
      << " to " << vehicle;

  const auto buildingId = c.GetBuildingId ();
  auto inv = buildingInv.Get (buildingId, c.GetOwner ());

  DropAllInventory (c, *inv);
  RemoveAllFitments (c, *inv);

  inv->GetInventory ().AddFungibleCount (c.GetProto ().vehicle (), 1);
  inv->GetInventory ().AddFungibleCount (vehicle, -1);
  c.MutableProto ().set_vehicle (vehicle);

  DeriveCharacterStats (c, ctx);
}

void
MoveProcessor::MaybeSetFitments (Character& c, const Json::Value& upd)
{
  std::vector<std::string> fitments;
  if (!ParseSetFitments (c, upd, fitments))
    return;

  VLOG (1) << "Changing fitments of character " << c.GetId ();

  const auto buildingId = c.GetBuildingId ();
  auto inv = buildingInv.Get (buildingId, c.GetOwner ());

  DropAllInventory (c, *inv);
  RemoveAllFitments (c, *inv);

  for (const auto& f : fitments)
    {
      inv->GetInventory ().AddFungibleCount (f, -1);
      c.MutableProto ().add_fitments (f);
    }

  DeriveCharacterStats (c, ctx);
}

void
MoveProcessor::MaybeFoundBuilding (Character& c, const Json::Value& upd)
{
  std::string type;
  proto::ShapeTransformation trafo;
  if (!ParseFoundBuilding (c, upd, type, trafo))
    return;

  VLOG (1)
      << "Building foundation for " << type
      << " at " << c.GetPosition ();

  auto b = buildings.CreateNew (type, c.GetOwner (), c.GetFaction ());
  b->SetCentre (c.GetPosition ());
  auto& pb = b->MutableProto ();
  pb.set_foundation (true);
  *pb.mutable_shape_trafo () = trafo;
  pb.mutable_age_data ()->set_founded_height (ctx.Height ());

  auto& inv = c.GetInventory ();
  const auto& roBuilding = ctx.RoConfig ().Building (b->GetType ());
  for (const auto& entry : roBuilding.construction ().foundation ())
    inv.AddFungibleCount (entry.first, -static_cast<int> (entry.second));

  UpdateBuildingStats (*b, ctx.Chain ());
  EnterBuilding (c, *b, dyn);

  /* EnterBuilding already removes the vehicle from dyn, but we have to add
     the building afterwards manually.  */
  dyn.AddBuilding (*b);
}

namespace
{

/**
 * Tries to move fungible items from one inventory (e.g. a character's)
 * to another (e.g. ground loot), based on the quantities given in the
 * map.  This verifies that there is enough in the "source" inventory,
 * and reduces the amount accordingly if not.
 *
 * If maxSpace is not -1, then only items using up at most that much space
 * will be transferred.  With this, we can e.g. limit the cargo space
 * of a character inventory.
 */
void
MoveFungibleBetweenInventories (const Context& ctx,
                                const FungibleAmountMap& items,
                                Inventory& from, Inventory& to,
                                const std::string& fromName,
                                const std::string& toName,
                                const int64_t maxSpace = -1)
{
  QuantityProduct usedSpace;

  for (const auto& entry : items)
    {
      const auto available = from.GetFungibleCount (entry.first);
      Quantity cnt = entry.second;
      if (cnt > available)
        {
          LOG (WARNING)
              << "Trying to move more of " << entry.first
              << " (" << cnt << ") than the existing " << available
              << " from " << fromName << " to " << toName;
          cnt = available;
        }

      if (maxSpace >= 0)
        {
          const auto itemSpace = ctx.RoConfig ().Item (entry.first).space ();

          if (itemSpace > 0)
            {
              const auto available = maxSpace - usedSpace.Extract ();
              CHECK_GE (available, 0);
              const auto maxForSpace = available / itemSpace;
              if (cnt > maxForSpace)
                {
                  LOG (WARNING)
                      << "Only moving " << maxForSpace << " of " << entry.first
                      << " instead of " << cnt
                      << " for lack of space (only " << available << " free)";
                  cnt = maxForSpace;
                }

              usedSpace.AddProduct (cnt, itemSpace);
            }

          CHECK (usedSpace <= maxSpace);
        }

      /* Avoid making the inventories dirty if we do not move anything.  */
      if (cnt == 0)
        continue;

      CHECK_LE (cnt, available);
      from.SetFungibleCount (entry.first, available - cnt);

      VLOG (1)
          << "Moved " << cnt << " of " << entry.first
          << " from " << fromName << " to " << toName;
      to.AddFungibleCount (entry.first, cnt);
    }

  CHECK (maxSpace == -1 || usedSpace <= maxSpace);
}

} // anonymous namespace

void
MoveProcessor::MaybeDropLoot (Character& c, const Json::Value& cmd)
{
  const auto fungible = ParseDropPickupFungible (cmd);
  if (fungible.empty ())
    return;

  std::ostringstream fromName;
  fromName << "character " << c.GetId ();
  std::ostringstream toName;

  if (c.IsInBuilding ())
    {
      toName << "building " << c.GetBuildingId ();
      auto b = buildings.GetById (c.GetBuildingId ());
      if (b->GetProto ().foundation ())
        {
          Inventory inv(*b->MutableProto ().mutable_construction_inventory ());
          MoveFungibleBetweenInventories (ctx, fungible,
                                          c.GetInventory (), inv,
                                          fromName.str (), toName.str ());

          /* Since items were added to the construction inventory, this may
             be the time to start construction itself.  */
          MaybeStartBuildingConstruction (*b, ongoings, ctx);
        }
      else
        {
          auto inv = buildingInv.Get (c.GetBuildingId (), c.GetOwner ());
          MoveFungibleBetweenInventories (ctx, fungible,
                                          c.GetInventory (),
                                          inv->GetInventory (),
                                          fromName.str (), toName.str ());
        }
    }
  else
    {
      toName << "ground loot at " << c.GetPosition ();
      auto ground = groundLoot.GetByCoord (c.GetPosition ());
      MoveFungibleBetweenInventories (ctx, fungible,
                                      c.GetInventory (),
                                      ground->GetInventory (),
                                      fromName.str (), toName.str ());
    }
}

void
MoveProcessor::MaybePickupLoot (Character& c, const Json::Value& cmd)
{
  const auto fungible = ParseDropPickupFungible (cmd);
  if (fungible.empty ())
    return;

  std::ostringstream fromName;
  std::ostringstream toName;
  toName << "character " << c.GetId ();

  const auto freeCargo = c.FreeCargoSpace (ctx.RoConfig ());
  VLOG (1)
      << "Character " << c.GetId () << " has " << freeCargo
      << " free cargo space before picking loot up with " << cmd;

  if (c.IsInBuilding ())
    {
      /* In principle, it is not possible to pick up stuff anyway
         in a foundation, because there won't be any inventory.  But still
         make it explicit, just to be safe.  */
      auto b = buildings.GetById (c.GetBuildingId ());
      if (b->GetProto ().foundation ())
        {
          LOG (WARNING)
              << "Character " << c.GetId ()
              << " can't pick up in " << b->GetId ()
              << " which is a foundation";
          return;
        }

      fromName << "building " << c.GetBuildingId ();
      auto inv = buildingInv.Get (c.GetBuildingId (), c.GetOwner ());
      MoveFungibleBetweenInventories (ctx, fungible,
                                      inv->GetInventory (),
                                      c.GetInventory (),
                                      fromName.str (), toName.str (),
                                      freeCargo);
    }
  else
    {
      fromName << "ground loot at " << c.GetPosition ();
      auto ground = groundLoot.GetByCoord (c.GetPosition ());
      MoveFungibleBetweenInventories (ctx, fungible,
                                      ground->GetInventory (),
                                      c.GetInventory (),
                                      fromName.str (), toName.str (),
                                      freeCargo);
    }
}

void
MoveProcessor::PerformCharacterUpdate (Character& c, const Json::Value& upd)
{
  MaybeTransferCharacter (c, upd);
  MaybeStartProspecting (c, upd);

  /* Updates to the vehicle and fitments are done before handling item
     pickups/drops, which means that we can place e.g. a cargo-expansion
     fitment and use it right away.  It also specifies the priorities when
     an item is picked up and at the same time put as fitment (it will
     be a fitment then).

     We change vehicle before fitments, so that one can change to a new
     vehicle and immediately equip fitments in a single move.  */
  MaybeChangeVehicle (c, upd);
  MaybeSetFitments (c, upd);

  /* Mining should be started before setting waypoints.  This ensures that if
     a move does both, we do not end up moving and mining at the same time
     (which is not allowed).  */
  MaybeStartMining (c, upd);

  /* We need to process speed updates after the waypoints, because a speed
     update is only valid if there is active movement.  That way, we can set
     waypoints and a chosen speed in a single move.  */
  MaybeSetCharacterWaypoints (c, upd);
  MaybeExtendCharacterWaypoints (c, upd);
  MaybeSetCharacterSpeed (c, upd);

  /* Founding a building puts the character into the newly created foundation.
     They may want to then drop resources there (for further construction),
     and/or exit immediately again.  */
  MaybeFoundBuilding (c, upd);

  /* Mobile refining is done before drop (and pickup).  This allows to drop
     the refined materials if desired right away, and also may free up
     cargo space for pickups.  */
  TryMobileRefining (c, upd);

  /* Dropping items is done before trying to pick items up.  This allows
     a player to drop stuff (and thus free cargo) before picking up something
     else in a single move.  */
  MaybeDropLoot (c, upd["drop"]);
  MaybePickupLoot (c, upd["pu"]);

  /* Entering a building is independent of any other moves, as it just sets
     a flag (but isn't by itself invalid e.g. if the character is busy).
     Exiting however takes effect immediately.  But since that puts the
     character on a random spot, it does not make much sense to combine
     other moves with it if exiting is done first (thus we do it last).
     In particular, this allows picking up stuff from inside the building
     and exiting in one move.

     Also, by processing "enter" before "exit", it means that sending both
     commands is equivalent to just enter (because we only set the flag and
     thus the exit move will be invalid).  This is more straight-forward
     than allowing to exit & enter again in the same move.  */
  MaybeEnterBuilding (c, upd);
  MaybeExitBuilding (c, upd);
}

void
MoveProcessor::PerformBuildingConfigUpdate (
    Building& b, const proto::Building::Config& newConfig)
{
  LOG (INFO)
      << "Scheduling building configuration update for " << b.GetId () << ":\n"
      << newConfig.DebugString ();

  auto op = ongoings.CreateNew (ctx.Height ());
  op->SetHeight (
      ctx.Height () + ctx.RoConfig ()->params ().building_update_delay ());
  op->SetBuildingId (b.GetId ());
  *op->MutableProto ().mutable_building_update ()->mutable_new_config ()
      = newConfig;
}

void
MoveProcessor::PerformBuildingTransfer (Building& b, const Account& newOwner)
{
  VLOG (1)
      << "Sending building " << b.GetId ()
      << " from " << b.GetOwner () << " to " << newOwner.GetName ();
  b.SetOwner (newOwner.GetName ());
}

void
MoveProcessor::PerformServiceOperation (ServiceOperation& op)
{
  op.Execute (rnd);
}

void
MoveProcessor::PerformDexOperation (DexOperation& op)
{
  op.Execute ();
}

namespace
{

/**
 * Tries to parse and execute a god-mode teleport command.
 */
void
MaybeGodTeleport (CharacterTable& tbl, const Json::Value& cmd)
{
  if (!cmd.isArray ())
    return;

  for (const auto& entry : cmd)
    {
      if (!entry.isObject ())
        {
          LOG (WARNING) << "Ignoring invalid teleport entry: " << entry;
          continue;
        }

      Database::IdT id;
      if (!IdFromJson (entry["id"], id))
        {
          LOG (WARNING) << "Invalid character ID in teleport: " << entry;
          continue;
        }

      HexCoord target;
      if (!CoordFromJson (entry["pos"], target))
        {
          LOG (WARNING) << "Invalid target in teleport: " << entry;
          continue;
        }

      auto c = tbl.GetById (id);
      if (c == nullptr)
        {
          LOG (WARNING)
              << "Character ID does not exist: " << id;
          continue;
        }

      LOG (INFO) << "Teleporting character " << id << " to: " << target;
      c->SetPosition (target);
      StopCharacter (*c);
    }
}

/**
 * Tries to parse and execute a god-mode command to set HP, either
 * for characters or buildings (depending on which type of table
 * is passed in).
 */
template <typename T>
  void
  MaybeGodSetHp (T& tbl, const Json::Value& cmd)
{
  if (!cmd.isArray ())
    return;

  for (const auto& entry : cmd)
    {
      if (!entry.isObject ())
        {
          LOG (WARNING) << "Ignoring invalid sethp entry: " << entry;
          continue;
        }

      Database::IdT id;
      if (!IdFromJson (entry["id"], id))
        {
          LOG (WARNING) << "Invalid ID in sethp: " << entry;
          continue;
        }

      auto c = tbl.GetById (id);
      if (c == nullptr)
        {
          LOG (WARNING) << "ID does not exist: " << id;
          continue;
        }

      LOG (INFO) << "Setting HP points for " << id << "...";
      auto& hp = c->MutableHP ();
      auto& maxHP = *c->MutableRegenData ().mutable_max_hp ();

      Json::Value val = entry["a"];
      if (xaya::IsIntegerValue (val) && val.isUInt64 ())
        hp.set_armour (val.asUInt64 ());
      val = entry["s"];
      if (xaya::IsIntegerValue (val) && val.isUInt64 ())
        hp.set_shield (val.asUInt64 ());

      val = entry["ma"];
      if (xaya::IsIntegerValue (val) && val.isUInt64 ())
        maxHP.set_armour (val.asUInt64 ());
      val = entry["ms"];
      if (xaya::IsIntegerValue (val) && val.isUInt64 ())
        maxHP.set_shield (val.asUInt64 ());
    }
}

/**
 * Tries to parse and execute a full "set HP" command, which is expected
 * to contain a list of buildings and/or characters to update.
 */
void
MaybeGodAllSetHp (BuildingsTable& b, CharacterTable& c, const Json::Value& cmd)
{
  if (!cmd.isObject ())
    return;

  MaybeGodSetHp (b, cmd["b"]);
  MaybeGodSetHp (c, cmd["c"]);
}

/**
 * Tries to parse and execute a god-mode command to create a building.
 */
void
MaybeGodBuild (AccountsTable& accounts, BuildingsTable& tbl, const Context& ctx,
               const Json::Value& cmd)
{
  if (!cmd.isArray ())
    return;

  for (const auto& build : cmd)
    {
      if (!build.isObject () || build.size () != 4)
        {
          LOG (WARNING) << "Invalid god-build element: " << build;
          continue;
        }

      std::string type;
      proto::ShapeTransformation trafo;
      if (!ParseBuildingConfig (ctx, build, type, trafo))
        {
          LOG (WARNING) << "Invalid god-build element: " << build;
          continue;
        }

      if (!build.isMember ("o"))
        {
          LOG (WARNING) << "God-build element has no owner field: " << build;
          continue;
        }
      const auto val = build["o"];
      Faction f;
      std::string owner;
      if (val.isNull ())
        f = Faction::ANCIENT;
      else if (val.isString ())
        {
          owner = val.asString ();
          auto h = accounts.GetByName (owner);
          if (h == nullptr || !h->IsInitialised ())
            {
              LOG (WARNING) << "Owner account does not exist: " << owner;
              continue;
            }
          f = h->GetFaction ();
        }
      else
        {
          LOG (WARNING) << "God-build element has invalid owner: " << build;
          continue;
        }

      HexCoord centre;
      if (!CoordFromJson (build["c"], centre))
        {
          LOG (WARNING) << "God-build element has invalid centre: " << build;
          continue;
        }

      /* Note that we do not check CanPlaceBuilding here on purpose.  It is ok
         if god-mode can in theory result in invalid situations.  But by not
         enforcing the conditions here we ensure that buildings can be easily
         placed as needed in tests, without having to worry about regions
         and the ability to build in them.  */

      auto b = tbl.CreateNew (type, owner, f);
      b->SetCentre (centre);
      auto& pb = b->MutableProto ();
      *pb.mutable_shape_trafo () = trafo;
      pb.mutable_age_data ()->set_founded_height (ctx.Height ());
      pb.mutable_age_data ()->set_finished_height (ctx.Height ());
      UpdateBuildingStats (*b, ctx.Chain ());
      LOG (INFO)
          << "God building " << type
          << " for " << owner << " of faction " << FactionToString (f) << ":\n"
          << "  id: " << b->GetId () << "\n"
          << "  centre: " << centre << "\n"
          << "  rotation: " << trafo.rotation_steps ();
    }
}

/**
 * Tries to parse a reference to a building inventory (building ID and
 * account name) for drop-loot command.
 */
bool
ParseBuildingInventory (const Json::Value& val,
                        Database::IdT& buildingId, std::string& name)
{
  if (!val.isObject () || val.size () != 2)
    return false;

  if (!IdFromJson (val["id"], buildingId))
    return false;

  const auto& nameVal = val["a"];
  if (!nameVal.isString ())
    return false;
  name = nameVal.asString ();

  return true;
}

/**
 * Tries to parse and execute a god-mode command that creates and drops
 * loot items on the ground.
 */
void
MaybeGodDropLoot (AccountsTable& accounts, GroundLootTable& loot,
                  BuildingInventoriesTable& buildingInv,
                  const Context& ctx, const Json::Value& cmd)
{
  if (!cmd.isArray ())
    return;

  for (const auto& tile : cmd)
    {
      if (!tile.isObject ())
        {
          LOG (WARNING) << "Drop-loot element is not an object: " << tile;
          continue;
        }

      const auto& fungible = tile["fungible"];
      if (!fungible.isObject ())
        {
          LOG (WARNING)
              << "Drop-loot element has invalid fungible member: " << tile;
          continue;
        }
      const auto quantities = ParseFungibleQuantities (ctx, fungible);

      if (tile.size () != 2)
        {
          LOG (WARNING) << "Drop-loot element has extra members: " << tile;
          continue;
        }

      Inventory* inv = nullptr;
      std::ostringstream where;

      /* See if this is dropping onto the ground.  */
      GroundLootTable::Handle gl;
      HexCoord pos;
      if (CoordFromJson (tile["pos"], pos))
        {
          gl = loot.GetByCoord (pos);
          inv = &gl->GetInventory ();
          where << pos;
        }

      /* Try dropping into a building inventory.  */
      BuildingInventoriesTable::Handle binv;
      Database::IdT buildingId;
      std::string name;
      if (ParseBuildingInventory (tile["building"], buildingId, name))
        {
          binv = buildingInv.Get (buildingId, name);
          inv = &binv->GetInventory ();
          where << " building " << buildingId << " / account " << name;

          /* Make sure the account entry of the recipient exists (but it
             is fine to leave it uninitialised).  */
          if (accounts.GetByName (name) == nullptr)
            accounts.CreateNew (name);
        }

      if (inv == nullptr)
        {
          LOG (WARNING)
              << "Drop-loot element has invalid/missing target: " << tile;
          continue;
        }

      for (const auto& entry : quantities)
        {
          LOG (INFO)
              << "God-mode dropping " << entry.second << " of " << entry.first
              << " at " << where.str ();
          inv->AddFungibleCount (entry.first, entry.second);
        }
    }
}

/**
 * Tries to parse and execute a god-mode command to add coins to account
 * balances in the game.
 */
void
MaybeGodGiftCoins (AccountsTable& tbl, MoneySupply& moneySupply,
                   const Json::Value& cmd)
{
  if (!cmd.isObject ())
    return;

  for (auto it = cmd.begin (); it != cmd.end (); ++it)
    {
      CHECK (it.key ().isString ());
      const std::string name = it.key ().asString ();

      auto a = tbl.GetByName (name);
      if (a == nullptr)
        a = tbl.CreateNew (name);

      Amount val;
      if (!CoinAmountFromJson (*it, val))
        {
          LOG (WARNING) << "God-mode gift of invalid amount: " << *it;
          continue;
        }

      LOG (INFO) << "Gifting " << val << " coins to " << name;
      a->AddBalance (val);
      moneySupply.Increment ("gifted", val);
    }
}

} // anonymous namespace

void
MoveProcessor::HandleGodMode (const Json::Value& cmd)
{
  if (!cmd.isObject ())
    return;

  if (!ctx.RoConfig ()->params ().god_mode ())
    {
      LOG (WARNING) << "God mode command ignored: " << cmd;
      return;
    }

  MaybeGodTeleport (characters, cmd["teleport"]);
  MaybeGodAllSetHp (buildings, characters, cmd["sethp"]);
  MaybeGodBuild (accounts, buildings, ctx, cmd["build"]);
  MaybeGodDropLoot (accounts, groundLoot, buildingInv, ctx, cmd["drop"]);
  MaybeGodGiftCoins (accounts, moneySupply, cmd["giftcoins"]);
}

void
MoveProcessor::MaybeInitAccount (const std::string& name,
                                 const Json::Value& init)
{
  if (!init.isObject ())
    return;

  auto a = accounts.GetByName (name);
  CHECK (a != nullptr);
  if (a->IsInitialised ())
    {
      LOG (WARNING) << "Account " << name << " is already initialised";
      return;
    }

  const auto& factionVal = init["faction"];
  if (!factionVal.isString ())
    {
      LOG (WARNING)
          << "Account initialisation does not specify faction: " << init;
      return;
    }
  const Faction faction = FactionFromString (factionVal.asString ());
  switch (faction)
    {
    case Faction::INVALID:
      LOG (WARNING) << "Invalid faction specified for account: " << init;
      return;

    case Faction::ANCIENT:
      LOG (WARNING) << "Account can't be ancient faction: " << init;
      return;

    default:
      break;
    }

  if (init.size () != 1)
    {
      LOG (WARNING) << "Account initialisation has extra fields: " << init;
      return;
    }

  a->SetFaction (faction);
  LOG (INFO)
      << "Initialised account " << name << " to faction "
      << FactionToString (faction);
}

void
MoveProcessor::TryAccountUpdate (const std::string& name,
                                 const Json::Value& upd)
{
  if (!upd.isObject ())
    return;

  MaybeInitAccount (name, upd["init"]);
}

void
MoveProcessor::TryCoinOperation (const std::string& name,
                                 const Json::Value& mv,
                                 Amount& burntChi)
{
  auto a = accounts.GetByName (name);
  CHECK (a != nullptr);

  CoinTransferBurn op;
  if (!ParseCoinTransferBurn (*a, mv, op, burntChi))
    return;

  if (op.minted > 0)
    {
      LOG (INFO) << name << " minted " << op.minted << " coins in the burnsale";
      a->AddBalance (op.minted);
      const Amount oldBurnsale = a->GetProto ().burnsale_balance ();
      a->MutableProto ().set_burnsale_balance (oldBurnsale + op.minted);
      moneySupply.Increment ("burnsale", op.minted);
    }

  if (op.burnt > 0)
    {
      LOG (INFO) << name << " is burning " << op.burnt << " coins";
      a->AddBalance (-op.burnt);
    }

  for (const auto& entry : op.transfers)
    {
      /* Transfers to self are a no-op, but we have to explicitly handle
         them here.  Else we would run into troubles by having a second
         active Account handle for the same account.  */
      if (entry.first == name)
        continue;

      LOG (INFO)
          << name << " is sending " << entry.second
          << " coins to " << entry.first;
      a->AddBalance (-entry.second);

      auto to = accounts.GetByName (entry.first);
      if (to == nullptr)
        {
          LOG (INFO)
              << "Creating uninitialised account for coin recipient "
              << entry.first;
          to = accounts.CreateNew (entry.first);
        }
      to->AddBalance (entry.second);
    }
}

/* ************************************************************************** */

} // namespace pxd
