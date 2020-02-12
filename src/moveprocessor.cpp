/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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
#include "prospecting.hpp"
#include "protoutils.hpp"
#include "spawn.hpp"

#include "database/faction.hpp"
#include "proto/character.pb.h"
#include "proto/roconfig.hpp"

#include <sstream>

namespace pxd
{

/* ************************************************************************** */

BaseMoveProcessor::BaseMoveProcessor (Database& d, const Context& c)
  : ctx(c), db(d),
    accounts(db), buildings(db), characters(db),
    groundLoot(db), regions(db, ctx.Height ())
{}

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
  if (outVal.isObject () && outVal.isMember (ctx.Params ().DeveloperAddress ()))
    CHECK (AmountFromJson (outVal[ctx.Params ().DeveloperAddress ()],
                           paidToDev));

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
      if (paidToDev < ctx.Params ().CharacterCost ())
        {
          /* In this case, we can return rather than continue with the next
             iteration.  If all money paid is "used up" already, then it won't
             be enough for later entries of the array, either.  */
          LOG (WARNING)
              << "Required amount for new character not paid by " << name
              << " (only have " << paidToDev << ")";
          return;
        }

      if (characters.CountForOwner (name) >= ctx.Params ().CharacterLimit ())
        {
          LOG (WARNING)
              << "Account " << name << " has the maximum number of characters"
              << " already, can't create another one";
          return;
        }

      PerformCharacterCreation (name, faction);
      paidToDev -= ctx.Params ().CharacterCost ();
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

  if (c.IsInBuilding ())
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is inside a building, can't set waypoints";
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
  if (!val.isUInt64 ())
    {
      LOG (WARNING) << "Not a building ID: " << val;
      return false;
    }
  buildingId = val.asUInt64 ();

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

namespace
{

/**
 * Parses a JSON dictionary giving fungible items and their quantities
 * into a std::map.  This will contain all item names and quantities
 * for "valid" entries, i.e. entries with a uint64 value that is within
 * the range [0, MAX_ITEM_QUANTITY].
 */
FungibleAmountMap
ParseFungibleQuantities (const Json::Value& obj)
{
  CHECK (obj.isObject ());
  const auto& itemData = RoConfigData ().fungible_items ();

  FungibleAmountMap res;
  for (auto it = obj.begin (); it != obj.end (); ++it)
    {
      const auto& keyVal = it.key ();
      CHECK (keyVal.isString ());
      const std::string key = keyVal.asString ();

      if (itemData.find (key) == itemData.end ())
        {
          LOG (WARNING) << "Invalid fungible item: " << key;
          continue;
        }

      if (!it->isUInt64 ())
        {
          LOG (WARNING)
              << "Invalid fungible amount for item " << key << ": " << *it;
          continue;
        }
      const Inventory::QuantityT cnt = it->asUInt64 ();

      CHECK_GE (cnt, 0);
      if (cnt == 0 || cnt > MAX_ITEM_QUANTITY)
        {
          LOG (WARNING)
              << "Invalid fungible amount for item " << key << ": " << cnt;
          continue;
        }

      const auto ins = res.emplace (key, cnt);
      CHECK (ins.second) << "Duplicate key: " << key;
    }

  return res;
}

} // anonymous namespace

FungibleAmountMap
BaseMoveProcessor::ParseDropPickupFungible (const Json::Value& cmd)
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

  return ParseFungibleQuantities (fungible);
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

  if (c.GetBusy () > 0)
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
  SpawnCharacter (name, f, characters, dyn, rnd, ctx);
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

  if (characters.CountForOwner (sendTo) >= ctx.Params ().CharacterLimit ())
    {
      LOG (WARNING)
          << "Account " << sendTo << " already has the maximum number of"
          << " characters, can't receive character " << c.GetId ();
      return;
    }

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
  if (c.GetProto ().has_mining ())
    {
      VLOG_IF (1, c.GetProto ().mining ().active ())
          << "Stopping mining with character " << c.GetId ()
          << " because of waypoints command";
      c.MutableProto ().mutable_mining ()->clear_active ();
    }

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
MoveProcessor::MaybeEnterBuilding (Character& c, const Json::Value& upd)
{
  Database::IdT buildingId;
  if (!ParseEnterBuilding (c, upd, buildingId))
    return;

  c.SetEnterBuilding (buildingId);
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
  c.SetBusy (ctx.Params ().ProspectingBlocks ());
  c.MutableProto ().mutable_prospection ();
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
MoveFungibleBetweenInventories (const FungibleAmountMap& items,
                                Inventory& from, Inventory& to,
                                const std::string& fromName,
                                const std::string& toName,
                                int64_t maxSpace = -1)
{
  const auto& itemData = RoConfigData ().fungible_items ();

  for (const auto& entry : items)
    {
      const auto available = from.GetFungibleCount (entry.first);
      Inventory::QuantityT cnt = entry.second;
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
          const auto mit = itemData.find (entry.first);
          CHECK (mit != itemData.end ())
              << "Unknown item to be transferred: " << entry.first;

          if (mit->second.space () > 0)
            {
              const auto maxForSpace = maxSpace / mit->second.space ();
              if (cnt > maxForSpace)
                {
                  LOG (WARNING)
                      << "Only moving " << maxForSpace << " of " << entry.first
                      << " instead of " << cnt
                      << " for lack of space (only " << maxSpace << " free)";
                  cnt = maxForSpace;
                }

              maxSpace -= Inventory::Product (cnt, mit->second.space ());
            }

          CHECK_GE (maxSpace, 0);
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
}

} // anonymous namespace

void
MoveProcessor::MaybeDropLoot (Character& c, const Json::Value& cmd)
{
  const auto fungible = ParseDropPickupFungible (cmd);
  if (fungible.empty ())
    return;

  if (c.IsInBuilding ())
    {
      LOG (WARNING) << "Drop/pickup inside building is ignored";
      return;
    }

  std::ostringstream fromName;
  fromName << "character " << c.GetId ();
  std::ostringstream toName;
  toName << "ground loot at " << c.GetPosition ();

  auto ground = groundLoot.GetByCoord (c.GetPosition ());
  MoveFungibleBetweenInventories (fungible,
                                  c.GetInventory (),
                                  ground->GetInventory (),
                                  fromName.str (), toName.str ());
}

void
MoveProcessor::MaybePickupLoot (Character& c, const Json::Value& cmd)
{
  const auto fungible = ParseDropPickupFungible (cmd);
  if (fungible.empty ())
    return;

  if (c.IsInBuilding ())
    {
      LOG (WARNING) << "Drop/pickup inside building is ignored";
      return;
    }

  std::ostringstream fromName;
  fromName << "ground loot at " << c.GetPosition ();
  std::ostringstream toName;
  toName << "character " << c.GetId ();

  const int64_t freeCargo = c.GetProto ().cargo_space () - c.UsedCargoSpace ();
  CHECK_GE (freeCargo, 0);
  VLOG (1)
      << "Character " << c.GetId () << " has " << freeCargo
      << " free cargo space before picking loot up with " << cmd;

  auto ground = groundLoot.GetByCoord (c.GetPosition ());
  MoveFungibleBetweenInventories (fungible,
                                  ground->GetInventory (),
                                  c.GetInventory (),
                                  fromName.str (), toName.str (),
                                  freeCargo);
}

void
MoveProcessor::PerformCharacterUpdate (Character& c, const Json::Value& upd)
{
  MaybeTransferCharacter (c, upd);
  MaybeStartProspecting (c, upd);

  /* Mining should be started before setting waypoints.  This ensures that if
     a move does both, we do not end up moving and mining at the same time
     (which is not allowed).  */
  MaybeStartMining (c, upd);

  /* We need to process speed updates after the waypoints, because a speed
     update is only valid if there is active movement.  That way, we can set
     waypoints and a chosen speed in a single move.  */
  MaybeSetCharacterWaypoints (c, upd);
  MaybeSetCharacterSpeed (c, upd);

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
     and exiting in one move.  */
  MaybeEnterBuilding (c, upd);
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
 * Tries to parse and execute a god-mode command to create a building.
 */
void
MaybeGodBuild (AccountsTable& accounts, BuildingsTable& tbl,
               const Json::Value& cmd)
{
  if (!cmd.isArray ())
    return;

  const auto& buildingTypes = RoConfigData ().building_types ();

  for (const auto& build : cmd)
    {
      if (!build.isObject () || build.size () != 4)
        {
          LOG (WARNING) << "Invalid god-build element: " << build;
          continue;
        }

      auto val = build["t"];
      if (!val.isString ())
        {
          LOG (WARNING) << "God-build element has invalid type: " << build;
          continue;
        }
      const std::string type = val.asString ();
      if (buildingTypes.find (type) == buildingTypes.end ())
        {
          LOG (WARNING) << "Invalid type for god build: " << type;
          continue;
        }

      if (!build.isMember ("o"))
        {
          LOG (WARNING) << "God-build element has no owner field: " << build;
          continue;
        }
      val = build["o"];
      Faction f;
      std::string owner;
      if (val.isNull ())
        f = Faction::ANCIENT;
      else if (val.isString ())
        {
          owner = val.asString ();
          auto h = accounts.GetByName (owner);
          if (h == nullptr)
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

      val = build["rot"];
      if (!val.isUInt () || val.asUInt () > 5)
        {
          LOG (WARNING) << "God-build element has invalid rotation: " << build;
          continue;
        }
      const unsigned rot = val.asUInt ();

      auto h = tbl.CreateNew (type, owner, f);
      h->SetCentre (centre);
      h->MutableProto ().mutable_shape_trafo ()->set_rotation_steps (rot);
      LOG (INFO)
          << "God building " << type
          << " for " << owner << " of faction " << FactionToString (f) << ":\n"
          << "  id: " << h->GetId () << "\n"
          << "  centre: " << centre << "\n"
          << "  rotation: " << rot;
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

      const auto quantities = ParseFungibleQuantities (fungible);
      auto h = tbl.GetByCoord (pos);
      for (const auto& entry : quantities)
        {
          LOG (INFO)
              << "God-mode dropping " << entry.second << " of " << entry.first
              << " at " << pos;
          h->GetInventory ().AddFungibleCount (entry.first, entry.second);
        }
    }
}

} // anonymous namespace

void
MoveProcessor::HandleGodMode (const Json::Value& cmd)
{
  if (!cmd.isObject ())
    return;

  if (!ctx.Params ().GodModeEnabled ())
    {
      LOG (WARNING) << "God mode command ignored: " << cmd;
      return;
    }

  MaybeGodTeleport (characters, cmd["teleport"]);
  MaybeGodSetHp (characters, cmd["sethp"]);
  MaybeGodBuild (accounts, buildings, cmd["build"]);
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
