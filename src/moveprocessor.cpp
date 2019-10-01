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

#include "moveprocessor.hpp"

#include "jsonutils.hpp"
#include "movement.hpp"
#include "protoutils.hpp"
#include "spawn.hpp"

#include "database/faction.hpp"
#include "proto/character.pb.h"

namespace pxd
{

/* ************************************************************************** */

bool
BaseMoveProcessor::ExtractMoveBasics (const Json::Value& moveObj,
                                      std::string& name, Json::Value& mv,
                                      Amount& paidToDev) const
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
  const auto& outVal = moveObj["out"];
  if (outVal.isObject () && outVal.isMember (params.DeveloperAddress ()))
    CHECK (AmountFromJson (outVal[params.DeveloperAddress ()], paidToDev));

  return true;
}

void
BaseMoveProcessor::TryCharacterCreation (const std::string& name,
                                         const Json::Value& mv,
                                         Amount paidToDev)
{
  const auto& cmd = mv["nc"];
  if (!cmd.isArray ())
    return;

  VLOG (1) << "Attempting to create new characters through move: " << cmd;

  const auto account = accounts.GetByName (name);
  CHECK (account != nullptr);
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
      if (paidToDev < params.CharacterCost ())
        {
          /* In this case, we can return rather than continue with the next
             iteration.  If all money paid is "used up" already, then it won't
             be enough for later entries of the array, either.  */
          LOG (WARNING)
              << "Required amount for new character not paid by " << name
              << " (only have " << paidToDev << ")";
          return;
        }

      PerformCharacterCreation (name, faction);
      paidToDev -= params.CharacterCost ();
      VLOG (1) << "After character creation, paid to dev left: " << paidToDev;
    }
}

void
BaseMoveProcessor::TryCharacterUpdates (const std::string& name,
                                        const Json::Value& mv)
{
  const auto& cmd = mv["c"];
  if (!cmd.isObject ())
    return;

  /* The order in which character updates are processed may be relevant for
     consensus (if the updates to characters interact with each other).  We
     order the updates in a move increasing by character ID.

     If a character ID has more than one update associated to it (e.g. because
     it appears in multiple, different ID lists used as keys), then the whole
     update is invalid and no part of it will be processed.  This simplifies
     handling, avoids any doubt about which one of the updates should be
     processed, and is something that is not relevant in practice anyway
     except for malicious moves or buggy software.

     For other errors (that do not lead to ambiguity) like a single malformed
     key string, a value that is not a JSON object or an ID that does not
     exist in the database, we only ignore that part.  */

  std::map<Database::IdT, Json::Value> updates;
  for (auto i = cmd.begin (); i != cmd.end (); ++i)
    {
      std::vector<Database::IdT> ids;
      if (!IdArrayFromString (i.name (), ids))
        {
          LOG (WARNING)
              << "Ignoring invalid character IDs for update: " << i.name ();
          continue;
        }

      const auto& upd = *i;
      if (!upd.isObject ())
        {
          LOG (WARNING)
              << "Character update is not an object: " << upd;
          continue;
        }

      for (const auto id : ids)
        {
          const auto res = updates.emplace (id, upd);
          if (!res.second)
            {
              LOG (WARNING)
                  << "Character update processes ID " << id
                  << " more than once: " << mv;
              return;
            }
        }
    }

  for (const auto& entry : updates)
    {
      auto c = characters.GetById (entry.first);
      if (c == nullptr)
        {
          LOG (WARNING)
              << "Character ID does not exist: " << entry.first;
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

      PerformCharacterUpdate (*c, entry.second);
    }
}

bool
BaseMoveProcessor::ParseCharacterWaypoints (const Character& c,
                                            const Json::Value& upd,
                                            std::vector<HexCoord>& wp)
{
  CHECK (upd.isObject ());
  const auto& wpArr = upd["wp"];
  if (!wpArr.isArray ())
    return false;

  if (c.GetBusy () > 0)
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't set waypoints";
      return false;
    }

  for (const auto& entry : wpArr)
    {
      HexCoord coord;
      if (!CoordFromJson (entry, coord))
        {
          LOG (WARNING)
              << "Invalid waypoints given for character " << c.GetId ()
              << ", not updating movement";
          return false;
        }
      wp.push_back (coord);
    }

  return true;
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

  if (c.GetBusy () > 0)
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't prospect";
      return false;
    }

  const auto& pos = c.GetPosition ();
  regionId = map.Regions ().GetRegionId (pos);
  VLOG (1)
      << "Character " << c.GetId ()
      << " is trying to prospect region " << regionId;

  auto r = regions.GetById (regionId);
  const auto& rpb = r->GetProto ();
  if (rpb.has_prospecting_character ())
    {
      LOG (WARNING)
          << "Region " << regionId
          << " is already being prospected by character "
          << rpb.prospecting_character ()
          << ", can't be prospected by " << c.GetId ();
      return false;
    }
  if (rpb.has_prospection ())
    {
      LOG (WARNING)
          << "Region " << regionId
          << " is already prospected, can't be prospected by " << c.GetId ();
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
  Amount paidToDev;
  if (!ExtractMoveBasics (moveObj, name, mv, paidToDev))
    return;

  /* We perform account updates first.  That ensures that it is possible to
     e.g. choose one's faction and create characters in a single move.  */
  TryAccountUpdate (name, mv["a"]);

  /* If there is no account (after potentially updating/initialising it),
     then let's not try to process any more updates.  This explicitly
     enforces that accounts have to be initialised before doing anything
     else, even if perhaps some changes wouldn't actually require access
     to an account in their processing.  */
  if (accounts.GetByName (name) == nullptr)
    {
      LOG (WARNING)
          << "Account " << name << " does not exist, ignoring move " << moveObj;
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
}

void
MoveProcessor::PerformCharacterCreation (const std::string& name,
                                         const Faction f)
{
  SpawnCharacter (name, f, characters, dyn, rnd, map, params);
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
  if (!val.isUInt64 ())
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

  const auto a = accounts.GetByName (sendTo);
  if (a == nullptr)
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

  auto* mv = c.MutableProto ().mutable_movement ();
  SetRepeatedCoords (wp, *mv->mutable_waypoints ());
}

void
MoveProcessor::MaybeStartProspecting (Character& c, const Json::Value& upd)
{
  Database::IdT regionId;
  if (!ParseCharacterProspecting (c, upd, regionId))
    return;

  auto r = regions.GetById (regionId);
  r->MutableProto ().set_prospecting_character (c.GetId ());

  StopCharacter (c);
  c.SetBusy (params.ProspectingBlocks ());
  c.MutableProto ().mutable_prospection ();
}

void
MoveProcessor::PerformCharacterUpdate (Character& c, const Json::Value& upd)
{
  MaybeTransferCharacter (c, upd);
  MaybeStartProspecting (c, upd);

  /* We need to process speed updates after the waypoints, because a speed
     update is only valid if there is active movement.  That way, we can set
     waypoints and a chosen speed in a single move.  */
  MaybeSetCharacterWaypoints (c, upd);
  MaybeSetCharacterSpeed (c, upd);
}

namespace
{

/**
 * Tries to parse and execute a god-mode teleport command.
 */
void
MaybeGodTeleport (CharacterTable& tbl, const Json::Value& cmd)
{
  if (!cmd.isObject ())
    return;

  for (auto i = cmd.begin (); i != cmd.end (); ++i)
    {
      Database::IdT id;
      if (!IdFromString (i.name (), id))
        {
          LOG (WARNING)
              << "Ignoring invalid character ID for teleport: " << i.name ();
          continue;
        }

      HexCoord target;
      if (!CoordFromJson (*i, target))
        {
          LOG (WARNING) << "Invalid teleport target: " << *i;
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
 * Tries to parse and execute a god-mode command to set HP.
 */
void
MaybeGodSetHp (CharacterTable& tbl, const Json::Value& cmd)
{
  if (!cmd.isObject ())
    return;

  for (auto i = cmd.begin (); i != cmd.end (); ++i)
    {
      Database::IdT id;
      if (!IdFromString (i.name (), id))
        {
          LOG (WARNING)
              << "Ignoring invalid character ID for sethp: " << i.name ();
          continue;
        }

      auto c = tbl.GetById (id);
      if (c == nullptr)
        {
          LOG (WARNING)
              << "Character ID does not exist: " << id;
          continue;
        }

      LOG (INFO) << "Setting HP points for " << id << "...";
      auto& hp = c->MutableHP ();
      auto& maxHP = *c->MutableRegenData ().mutable_max_hp ();

      Json::Value val = (*i)["a"];
      if (val.isUInt64 ())
        hp.set_armour (val.asUInt64 ());
      val = (*i)["s"];
      if (val.isUInt64 ())
        hp.set_shield (val.asUInt64 ());

      val = (*i)["ma"];
      if (val.isUInt64 ())
        maxHP.set_armour (val.asUInt64 ());
      val = (*i)["ms"];
      if (val.isUInt64 ())
        maxHP.set_shield (val.asUInt64 ());
    }
}

/**
 * Tries to parse and execute a god-mode command that creates and drops
 * loot items on the ground.
 */
void
MaybeGodDropLoot (GroundLootTable& tbl, const Json::Value& cmd)
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

      HexCoord pos;
      if (!CoordFromJson (tile["pos"], pos))
        {
          LOG (WARNING)
              << "Drop-loot element has invalid position: " << tile;
          continue;
        }

      const auto& fungible = tile["fungible"];
      if (!fungible.isObject ())
        {
          LOG (WARNING)
              << "Drop-loot element has invalid fungible member: " << tile;
          continue;
        }

      if (tile.size () != 2)
        {
          LOG (WARNING) << "Drop-loot element has extra members: " << tile;
          continue;
        }

      auto h = tbl.GetByCoord (pos);
      for (auto it = fungible.begin (); it != fungible.end (); ++it)
        {
          const auto& keyVal = it.key ();
          CHECK (keyVal.isString ());
          const std::string key = keyVal.asString ();

          if (!it->isUInt64 () || it->asUInt64 () > MAX_ITEM_QUANTITY)
            {
              LOG (WARNING)
                  << "Invalid fungible amount for item " << key << ": " << *it;
              continue;
            }
          const Inventory::QuantityT cnt = it->asUInt64 ();

          const auto before = h->GetInventory ().GetFungibleCount (key);
          LOG (INFO)
              << "God-mode dropping " << cnt << " of " << key << " at " << pos
              << " in addition to existing " << before;
          h->GetInventory ().SetFungibleCount (key, before + cnt);
        }
    }
}

} // anonymous namespace

void
MoveProcessor::HandleGodMode (const Json::Value& cmd)
{
  if (!cmd.isObject ())
    return;

  if (!params.GodModeEnabled ())
    {
      LOG (WARNING) << "God mode command ignored: " << cmd;
      return;
    }

  MaybeGodTeleport (characters, cmd["teleport"]);
  MaybeGodSetHp (characters, cmd["sethp"]);
  MaybeGodDropLoot (groundLoot, cmd["drop"]);
}

void
MoveProcessor::MaybeInitAccount (const std::string& name,
                                 const Json::Value& init)
{
  if (!init.isObject ())
    return;

  if (accounts.GetByName (name) != nullptr)
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
  if (faction == Faction::INVALID)
    {
      LOG (WARNING) << "Invalid faction specified for account: " << init;
      return;
    }

  if (init.size () != 1)
    {
      LOG (WARNING) << "Account initialisation has extra fields: " << init;
      return;
    }

  accounts.CreateNew (name, faction);
  LOG (INFO)
      << "Created account " << name << " of faction "
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

/* ************************************************************************** */

} // namespace pxd
