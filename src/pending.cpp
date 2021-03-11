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

#include "pending.hpp"

#include "gamestatejson.hpp"
#include "jsonutils.hpp"
#include "protoutils.hpp"
#include "logic.hpp"

#include <type_traits>

namespace pxd
{

/* ************************************************************************** */

void
PendingState::Clear ()
{
  buildings.clear ();
  characters.clear ();
  newCharacters.clear ();
  accounts.clear ();
}

PendingState::BuildingState&
PendingState::GetBuildingState (const Building& b)
{
  const auto id = b.GetId ();

  const auto mit = buildings.find (id);
  if (mit == buildings.end ())
    {
      const auto ins = buildings.emplace (id, BuildingState ());
      CHECK (ins.second);
      VLOG (1)
          << "Building " << id << " was not yet pending, added pending entry";
      return ins.first->second;
    }

  VLOG (1) << "Building " << id << " is already pending, updating entry";
  return mit->second;
}

PendingState::CharacterState&
PendingState::GetCharacterState (const Character& c)
{
  const auto id = c.GetId ();

  const auto mit = characters.find (id);
  if (mit == characters.end ())
    {
      const auto ins = characters.emplace (id, CharacterState ());
      CHECK (ins.second);
      VLOG (1)
          << "Character " << id << " was not yet pending, added pending entry";
      return ins.first->second;
    }

  VLOG (1) << "Character " << id << " is already pending, updating entry";
  return mit->second;
}

PendingState::AccountState&
PendingState::GetAccountState (const Account& a)
{
  const auto& name = a.GetName ();

  const auto mit = accounts.find (name);
  if (mit == accounts.end ())
    {
      const auto ins = accounts.emplace (name, AccountState ());
      CHECK (ins.second);
      VLOG (1)
          << "Account " << name << " was not yet pending, adding pending entry";
      return ins.first->second;
    }

  VLOG (1) << "Account " << name << " is already pending, updating entry";
  return mit->second;
}

void
PendingState::AddBuildingConfig (const Building& b,
                                 const proto::Building::Config& newConfig)
{
  VLOG (1)
      << "Adding pending building config for " << b.GetId ()
      << ":\n" << newConfig.DebugString ();
  auto& state = GetBuildingState (b);
  state.newConfig.MergeFrom (newConfig);
}

void
PendingState::AddBuildingTransfer (const Building& b,
                                   const std::string& newOwner)
{
  VLOG (1)
      << "Adding pending building transfer of " << b.GetId ()
      << " to account " << newOwner;
  GetBuildingState (b).sentTo = newOwner;
}

void
PendingState::AddCharacterWaypoints (const Character& ch,
                                     const std::vector<HexCoord>& wp,
                                     const bool replace)
{
  VLOG (1) << "Adding pending waypoints for character " << ch.GetId ();
  auto& chState = GetCharacterState (ch);

  if (chState.prospectingRegionId != RegionMap::OUT_OF_MAP)
    {
      LOG (WARNING)
          << "Character " << ch.GetId ()
          << " is pending to start prospecting, ignoring waypoints";
      return;
    }

  /* When setting waypoints, a potential minig operation is stopped.  Thus
     assume that the character will not start mining if we set waypoints
     (likely) after the mining move gets confirmed.  */
  if (chState.miningRegionId != RegionMap::OUT_OF_MAP)
    {
      LOG (WARNING)
          << "Character " << ch.GetId ()
          << " is setting waypoints, we'll not start mining";
      chState.miningRegionId = RegionMap::OUT_OF_MAP;
    }

  if (chState.wp == nullptr || replace)
    {
      chState.wp = std::make_unique<std::vector<HexCoord>> ();
      /* If we are not replacing (i.e. extending), copy any existing
         waypoints from the confirmed state first.  */
      if (!replace)
        for (const auto& c : ch.GetProto ().movement ().waypoints ())
          chState.wp->push_back (CoordFromProto (c));
    }

  chState.wp->insert (chState.wp->end (), wp.begin (), wp.end ());
}

void
PendingState::AddEnterBuilding (const Character& ch,
                                const Database::IdT buildingId)
{
  VLOG (1) << "Adding enter-building command for character " << ch.GetId ();
  auto& chState = GetCharacterState (ch);

  chState.hasEnterBuilding = true;
  chState.enterBuilding = buildingId;
}

void
PendingState::AddExitBuilding (const Character& ch)
{
  VLOG (1) << "Adding exit-building command for character " << ch.GetId ();
  GetCharacterState (ch).exitBuilding = ch.GetBuildingId ();
}

void
PendingState::AddCharacterDrop (const Character& ch)
{
  VLOG (1) << "Adding pending item drop for character " << ch.GetId ();
  GetCharacterState (ch).drop = true;
}

void
PendingState::AddCharacterPickup (const Character& ch)
{
  VLOG (1) << "Adding pending item pickup for character " << ch.GetId ();
  GetCharacterState (ch).pickup = true;
}

void
PendingState::AddCharacterProspecting (const Character& ch,
                                       const Database::IdT regionId)
{
  VLOG (1)
      << "Character " << ch.GetId ()
      << " is pending to start prospecting region " << regionId;

  auto& chState = GetCharacterState (ch);

  /* If there is already a pending region, then it will be the same ID.
     That is because the ID is set from the character's current position, and
     that can not change between blocks (when the pending state is rebuilt from
     scratch anyway).  */
  if (chState.prospectingRegionId != RegionMap::OUT_OF_MAP)
    CHECK_EQ (chState.prospectingRegionId, regionId)
        << "Character " << ch.GetId ()
        << " is pending to prospect another region";

  chState.prospectingRegionId = regionId;

  /* Clear any waypoints that are pending.  This assumes that both moves
     will be confirmed at the same time (i.e. not just the movement), but
     that is the best guess we can make.  */
  if (chState.wp != nullptr)
    {
      LOG (WARNING)
          << "Character " << ch.GetId ()
          << " will start prospecting, clearing pending waypoints";
      chState.wp.reset ();
    }
}

void
PendingState::AddCharacterMining (const Character& ch,
                                  const Database::IdT regionId)
{
  VLOG (1)
      << "Character " << ch.GetId ()
      << " is pending to start mining region " << regionId;

  auto& chState = GetCharacterState (ch);

  if (chState.prospectingRegionId != RegionMap::OUT_OF_MAP)
    {
      LOG (WARNING)
          << "Character " << ch.GetId () << " will start prospecting,"
             " can't start mining as well";
      return;
    }

  if (chState.wp != nullptr)
    {
      LOG (WARNING)
          << "Character " << ch.GetId () << " has pending waypoints,"
             " can't start mining";
      return;
    }

  /* If there is already a pending mining region, it has to be the same ID since
     the character position can't change.  */
  if (chState.miningRegionId != RegionMap::OUT_OF_MAP)
    CHECK_EQ (chState.miningRegionId, regionId)
        << "Character " << ch.GetId ()
        << " is pending to mine another region";

  chState.miningRegionId = regionId;
}

void
PendingState::AddFoundBuilding (const Character& ch, const std::string& type,
                                const proto::ShapeTransformation& trafo)
{
  auto& chState = GetCharacterState (ch);

  /* In theory, there are situations in which a single character can found
     two buildings in the same block:  They can found a building, then exit
     it (even in the same move), and then found another one at the place
     they'll end up at.  But this is not something we care about (or even
     can properly predict) in pending tracking, so just ignore all further
     found-building moves.  */
  if (!chState.foundBuilding.isNull ())
    {
      LOG (WARNING)
          << "Character " << ch.GetId ()
          << " already has a pending 'found building' move, ignoring next";
      return;
    }

  VLOG (1) << "Character " << ch.GetId () << " is founding " << type;
  chState.foundBuilding = Json::Value (Json::objectValue);
  chState.foundBuilding["type"] = type;
  chState.foundBuilding["rotationsteps"] = IntToJson (trafo.rotation_steps ());
}

void
PendingState::AddCharacterVehicle (const Character& ch,
                                   const std::string& vehicle)
{
  VLOG (1) << "Character " << ch.GetId () << " changes to vehicle " << vehicle;
  GetCharacterState (ch).changeVehicle = vehicle;
}

void
PendingState::AddCharacterFitments (const Character& ch,
                                    const std::vector<std::string>& fitments)
{
  VLOG (1) << "Character " << ch.GetId () << " has pending fitments";

  auto& chState = GetCharacterState (ch);
  chState.fitments = Json::Value(Json::arrayValue);
  for (const auto& f : fitments)
    chState.fitments.append (f);
}

void
PendingState::AddCharacterCreation (const std::string& name, const Faction f)
{
  VLOG (1)
      << "Processing pending character creation for " << name
      << ": Faction " << FactionToString (f);

  auto mit = newCharacters.find (name);
  if (mit == newCharacters.end ())
    {
      const auto ins = newCharacters.emplace (name,
                                              std::vector<NewCharacter> ());
      CHECK (ins.second);
      mit = ins.first;
    }

  mit->second.push_back (NewCharacter (f));
}

void
PendingState::AddCoinTransferBurn (const Account& a, const CoinTransferBurn& op)
{
  VLOG (1) << "Adding pending coin operation for " << a.GetName ();

  auto& aState = GetAccountState (a);

  if (aState.coinOps == nullptr)
    {
      aState.coinOps = std::make_unique<CoinTransferBurn> (op);
      return;
    }

  aState.coinOps->minted += op.minted;
  aState.coinOps->burnt += op.burnt;
  for (const auto& entry : op.transfers)
    aState.coinOps->transfers[entry.first] += entry.second;
}

void
PendingState::AddServiceOperation (const ServiceOperation& op)
{
  const auto val = op.ToPendingJson ();

  VLOG (1)
      << "Adding pending service operation for "
      << op.GetAccount ().GetName () << ":\n" << val;

  GetAccountState (op.GetAccount ()).serviceOps.push_back (val);
}

void
PendingState::AddDexOperation (const DexOperation& op)
{
  const auto val = op.ToPendingJson ();

  VLOG (1)
      << "Adding pending DEX operation for "
      << op.GetAccount ().GetName () << ":\n" << val;

  GetAccountState (op.GetAccount ()).dexOps.push_back (val);
}

bool
PendingState::HasPendingWaypoints (const Character& c) const
{
  const auto mit = characters.find (c.GetId ());
  if (mit == characters.end ())
    return false;

  if (mit->second.wp == nullptr)
    return false;

  return !mit->second.wp->empty ();
}

Json::Value
PendingState::BuildingState::ToJson () const
{
  Json::Value res(Json::objectValue);

  const auto cfg = GameStateJson::Convert (newConfig);
  if (!cfg.empty ())
    res["newconfig"] = cfg;

  if (!sentTo.empty ())
    res["sentto"] = sentTo;

  return res;
}

Json::Value
PendingState::CharacterState::ToJson () const
{
  Json::Value res(Json::objectValue);

  if (wp != nullptr)
    {
      Json::Value wpJson(Json::arrayValue);
      for (const auto& c : *wp)
        wpJson.append (CoordToJson (c));

      res["waypoints"] = wpJson;
    }

  if (hasEnterBuilding)
    {
      if (enterBuilding == Database::EMPTY_ID)
        res["enterbuilding"] = Json::Value ();
      else
        res["enterbuilding"] = IntToJson (enterBuilding);
    }
  if (exitBuilding != Database::EMPTY_ID)
    {
      Json::Value exit(Json::objectValue);
      exit["building"] = IntToJson (exitBuilding);
      res["exitbuilding"] = exit;
    }

  res["drop"] = drop;
  res["pickup"] = pickup;

  if (prospectingRegionId != RegionMap::OUT_OF_MAP)
    res["prospecting"] = IntToJson (prospectingRegionId);
  if (miningRegionId != RegionMap::OUT_OF_MAP)
    res["mining"] = IntToJson (miningRegionId);

  if (!foundBuilding.isNull ())
    res["foundbuilding"] = foundBuilding;

  if (!changeVehicle.empty ())
    res["changevehicle"] = changeVehicle;
  if (!fitments.isNull ())
    res["fitments"] = fitments;

  return res;
}

Json::Value
PendingState::NewCharacter::ToJson () const
{
  Json::Value res(Json::objectValue);
  res["faction"] = FactionToString (faction);

  return res;
}

Json::Value
PendingState::AccountState::ToJson () const
{
  Json::Value res(Json::objectValue);

  if (coinOps != nullptr)
    {
      Json::Value coin(Json::objectValue);
      coin["minted"] = IntToJson (coinOps->minted);
      coin["burnt"] = IntToJson (coinOps->burnt);

      Json::Value transfers(Json::objectValue);
      for (const auto& entry : coinOps->transfers)
        transfers[entry.first] = IntToJson (entry.second);
      coin["transfers"] = transfers;

      res["coinops"] = coin;
    }

  if (!serviceOps.empty ())
    {
      Json::Value ops(Json::arrayValue);
      for (const auto& o : serviceOps)
        ops.append (o);
      res["serviceops"] = ops;
    }

  if (!dexOps.empty ())
    {
      Json::Value ops(Json::arrayValue);
      for (const auto& o : dexOps)
        ops.append (o);
      res["dexops"] = ops;
    }

  return res;
}

namespace
{

/**
 * Converts a map of entries (building, character, account states) to
 * a JSON array.
 */
template <typename Map>
  Json::Value
  StateMapToJsonArray (const Map& m, const std::string& keyField)
{
  Json::Value res(Json::arrayValue);
  for (const auto& entry : m)
    {
      auto val = entry.second.ToJson ();
      if (std::is_integral<typename Map::key_type>::value)
        val[keyField] = IntToJson (entry.first);
      else
        val[keyField] = entry.first;
      res.append (val);
    }
  return res;
}

} // anonymous namespace

Json::Value
PendingState::ToJson () const
{
  Json::Value res(Json::objectValue);

  res["buildings"] = StateMapToJsonArray (buildings, "id");
  res["characters"] = StateMapToJsonArray (characters, "id");
  res["accounts"] = StateMapToJsonArray (accounts, "name");

  Json::Value newCh(Json::arrayValue);
  for (const auto& entry : newCharacters)
    {
      Json::Value curName(Json::objectValue);
      curName["name"] = entry.first;

      Json::Value arr(Json::arrayValue);
      for (const auto& nc : entry.second)
        arr.append (nc.ToJson ());
      curName["creations"] = arr;

      newCh.append (curName);
    }
  res["newcharacters"] = newCh;

  return res;
}

/* ************************************************************************** */

void
PendingStateUpdater::PerformBuildingConfigUpdate (
    Building& b, const proto::Building::Config& newConfig)
{
  state.AddBuildingConfig (b, newConfig);
}

void
PendingStateUpdater::PerformBuildingTransfer (Building& b,
                                              const Account& newOwner)
{
  state.AddBuildingTransfer (b, newOwner.GetName ());
}

void
PendingStateUpdater::PerformCharacterCreation (Account& acc, const Faction f)
{
  state.AddCharacterCreation (acc.GetName (), f);
}

void
PendingStateUpdater::PerformCharacterUpdate (Character& c,
                                             const Json::Value& upd)
{
  BuildingsTable::Handle b;
  if (c.IsInBuilding ())
    b = buildings.GetById (c.GetBuildingId ());

  Database::IdT regionId;
  if (ParseCharacterProspecting (c, upd, regionId))
    state.AddCharacterProspecting (c, regionId);

  if (ParseCharacterMining (c, upd, regionId))
    state.AddCharacterMining (c, regionId);

  FungibleAmountMap items;
  items = ParseDropPickupFungible (upd["pu"]);
  if (!items.empty ())
    {
      if (b != nullptr && b->GetProto ().foundation ())
        LOG (WARNING)
            << "Ignoring pending move for character " << c.GetId ()
            << " to pick up in foundation " << b->GetId ();
      else
        state.AddCharacterPickup (c);
    }
  items = ParseDropPickupFungible (upd["drop"]);
  if (!items.empty ())
    state.AddCharacterDrop (c);

  std::vector<HexCoord> wp;
  if (ParseCharacterWaypoints (c, upd, wp))
    {
      VLOG (1)
          << "Found pending waypoints for character " << c.GetId ()
          << ": " << upd["wp"];
      state.AddCharacterWaypoints (c, wp, true);
    }
  wp.clear ();
  if (ParseCharacterWaypointExtension (c, upd,
                                       state.HasPendingWaypoints (c), wp))
    {
      VLOG (1)
          << "Found pending waypoints extension for " << c.GetId ()
          << ": " << upd["wpx"];
      state.AddCharacterWaypoints (c, wp, false);
    }

  Database::IdT buildingId;
  if (ParseEnterBuilding (c, upd, buildingId))
    state.AddEnterBuilding (c, buildingId);
  if (ParseExitBuilding (c, upd))
    state.AddExitBuilding (c);

  std::string type;
  proto::ShapeTransformation trafo;
  if (ParseFoundBuilding (c, upd, type, trafo))
    state.AddFoundBuilding (c, type, trafo);

  std::string vehicle;
  if (ParseChangeVehicle (c, upd, vehicle))
    state.AddCharacterVehicle (c, vehicle);
  std::vector<std::string> fitments;
  if (ParseSetFitments (c, upd, fitments))
    state.AddCharacterFitments (c, fitments);

  TryMobileRefining (c, upd);
}

void
PendingStateUpdater::PerformServiceOperation (ServiceOperation& op)
{
  state.AddServiceOperation (op);
}

void
PendingStateUpdater::PerformDexOperation (DexOperation& op)
{
  state.AddDexOperation (op);
}

void
PendingStateUpdater::ProcessMove (const Json::Value& moveObj)
{
  std::string name;
  Json::Value mv;
  Amount paidToDev, burnt;
  if (!ExtractMoveBasics (moveObj, name, mv, paidToDev, burnt))
    return;

  auto a = accounts.GetByName (name);
  if (a == nullptr)
    {
      /* This is also triggered for moves actually registering an account,
         so it not something really "bad" we need to warn about.  */
      VLOG (1)
          << "Account " << name
          << " does not exist, ignoring pending move " << moveObj;
      return;
    }
  const bool accountInit = a->IsInitialised ();

  CoinTransferBurn coinOps;
  if (ParseCoinTransferBurn (*a, mv, coinOps, burnt))
    state.AddCoinTransferBurn (*a, coinOps);

  /* Release the account again.  It is not needed anymore, and some of
     the further operations may allocate another Account handle for
     the current name (while it is not allowed to have two active ones
     in parallel).  */
  a.reset ();

  TryDexOperations (name, mv);

  /* If the account is not initialised yet, any other action is invalid anyway.
     If this is the init move itself, they would be actually fine, but we
     ignore this edge case for pending processing.  */
  if (!accountInit)
    return;

  TryCharacterUpdates (name, mv);
  TryCharacterCreation (name, mv, paidToDev);

  TryBuildingUpdates (name, mv);
  TryServiceOperations (name, mv);
}

/* ************************************************************************** */

PendingMoves::PendingMoves (PXLogic& rules)
  : xaya::SQLiteGame::PendingMoves(rules)
{}

void
PendingMoves::Clear ()
{
  state.Clear ();
  dyn.reset ();
}

void
PendingMoves::AddPendingMove (const Json::Value& mv)
{
  auto& db = const_cast<xaya::SQLiteDatabase&> (AccessConfirmedState ());
  PXLogic& rules = dynamic_cast<PXLogic&> (GetSQLiteGame ());
  SQLiteGameDatabase dbObj(db, rules);

  const auto& blk = GetConfirmedBlock ();
  const auto& heightVal = blk["height"];
  CHECK (heightVal.isUInt ());

  const Context ctx(GetChain (), rules.GetBaseMap (),
                    heightVal.asUInt () + 1, Context::NO_TIMESTAMP);

  if (dyn == nullptr)
    dyn = std::make_unique<DynObstacles> (dbObj, ctx);

  PendingStateUpdater updater(dbObj, *dyn, state, ctx);
  updater.ProcessMove (mv);
}

Json::Value
PendingMoves::ToJson () const
{
  return state.ToJson ();
}

/* ************************************************************************** */

} // namespace pxd
