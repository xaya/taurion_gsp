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

void
PendingState::AddCharacterWaypoints (const Character& ch,
                                     const std::vector<HexCoord>& wp)
{
  VLOG (1) << "Adding pending waypoints for character " << ch.GetId ();

  auto mit = characters.find (ch.GetId ());
  if (mit == characters.end ())
    {
      const auto ins = characters.emplace (ch.GetId (), CharacterState ());
      CHECK (ins.second);
      mit = ins.first;
      VLOG (1) << "Character was not yet pending, added pending entry";
    }
  else
    VLOG (1) << "Character is already pending, updating entry";

  mit->second.wp = std::make_unique<std::vector<HexCoord>> (wp);
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

  Json::Value newCh(Json::objectValue);
  for (const auto& entry : newCharacters)
    {
      Json::Value arr(Json::arrayValue);
      for (const auto& nc : entry.second)
        arr.append (nc.ToJson ());
      newCh[entry.first] = arr;
    }
  res["newcharacters"] = newCh;

  return res;
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
  LOG (WARNING) << "Ignoring pending move: " << mv;
}

Json::Value
PendingMoves::ToJson () const
{
  return state.ToJson ();
}

/* ************************************************************************** */

} // namespace pxd
