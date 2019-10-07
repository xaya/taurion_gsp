/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#include "jsonutils.hpp"
#include "logic.hpp"

namespace pxd
{

/* ************************************************************************** */

void
PendingState::Clear ()
{
  characters.clear ();
  newCharacters.clear ();
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

void
PendingState::AddCharacterWaypoints (const Character& ch,
                                     const std::vector<HexCoord>& wp)
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

  chState.wp = std::make_unique<std::vector<HexCoord>> (wp);
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

  if (prospectingRegionId != RegionMap::OUT_OF_MAP)
    res["prospecting"] = IntToJson (prospectingRegionId);

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
PendingState::ToJson () const
{
  Json::Value res(Json::objectValue);

  Json::Value chJson(Json::arrayValue);
  for (const auto& entry : characters)
    {
      auto val = entry.second.ToJson ();
      val["id"] = IntToJson (entry.first);
      chJson.append (val);
    }
  res["characters"] = chJson;

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
PendingStateUpdater::PerformCharacterCreation (const std::string& name,
                                               const Faction f)
{
  state.AddCharacterCreation (name, f);
}

void
PendingStateUpdater::PerformCharacterUpdate (Character& c,
                                             const Json::Value& upd)
{
  Database::IdT regionId;
  if (ParseCharacterProspecting (c, upd, regionId))
    state.AddCharacterProspecting (c, regionId);

  std::vector<HexCoord> wp;
  if (ParseCharacterWaypoints (c, upd, wp))
    {
      VLOG (1)
          << "Found pending waypoints for character " << c.GetId ()
          << ": " << upd["wp"];
      state.AddCharacterWaypoints (c, wp);
    }
}

void
PendingStateUpdater::ProcessMove (const Json::Value& moveObj)
{
  std::string name;
  Json::Value mv;
  Amount paidToDev;
  if (!ExtractMoveBasics (moveObj, name, mv, paidToDev))
    return;

  if (accounts.GetByName (name) == nullptr)
    {
      /* This is also triggered for moves actually registering an account,
         so it not something really "bad" we need to warn about.  */
      VLOG (1)
          << "Account " << name
          << " does not exist, ignoring pending move " << moveObj;
      return;
    }

  TryCharacterUpdates (name, mv);
  TryCharacterCreation (name, mv, paidToDev);
}

/* ************************************************************************** */

PendingMoves::PendingMoves (PXLogic& rules)
  : xaya::SQLiteGame::PendingMoves(rules)
{}

void
PendingMoves::Clear ()
{
  state.Clear ();
}

void
PendingMoves::AddPendingMove (const Json::Value& mv)
{
  AccessConfirmedState ();
  PXLogic& rules = dynamic_cast<PXLogic&> (GetSQLiteGame ());
  SQLiteGameDatabase dbObj(rules);

  const Context ctx(GetChain (), rules.GetBaseMap (),
                    GetConfirmedHeight () + 1, Context::NO_TIMESTAMP);

  PendingStateUpdater updater(dbObj, state, ctx);
  updater.ProcessMove (mv);
}

Json::Value
PendingMoves::ToJson () const
{
  return state.ToJson ();
}

/* ************************************************************************** */

} // namespace pxd
