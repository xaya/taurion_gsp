#include "moveprocessor.hpp"

#include "jsonutils.hpp"
#include "protoutils.hpp"

#include "database/faction.hpp"
#include "proto/character.pb.h"

namespace pxd
{

void
MoveProcessor::ProcessAll (const Json::Value& moveArray)
{
  CHECK (moveArray.isArray ());
  for (const auto& m : moveArray)
    ProcessOne (m);
}

void
MoveProcessor::ProcessOne (const Json::Value& moveObj)
{
  VLOG (1) << "Processing move:\n" << moveObj;
  CHECK (moveObj.isObject ());

  CHECK (moveObj.isMember ("move"));
  const Json::Value& mv = moveObj["move"];
  if (!mv.isObject ())
    {
      LOG (WARNING) << "Move is not an object: " << mv;
      return;
    }

  const auto& nameVal = moveObj["name"];
  CHECK (nameVal.isString ());
  const std::string name = nameVal.asString ();

  Amount paidToDev = 0;
  const auto& outVal = moveObj["out"];
  if (outVal.isObject () && outVal.isMember (params.DeveloperAddress ()))
    CHECK (AmountFromJson (outVal[params.DeveloperAddress ()], paidToDev));

  /* Note that the order between character update and character creation
     matters:  By having the update *before* the creation, we explicitly
     forbid a situation in which a newly created character is updated right
     away.  That would be tricky (since the ID would have to be predicated),
     but it would have been possible sometimes if the order were reversed.
     We want to exclude such trickery and thus do the update first.  */
  HandleCharacterUpdate (name, mv);
  HandleCharacterCreation (name, mv, paidToDev);
}

void
MoveProcessor::HandleCharacterCreation (const std::string& name,
                                        const Json::Value& mv,
                                        const Amount paidToDev)
{
  const auto& cmd = mv["nc"];
  if (!cmd.isObject ())
    return;

  VLOG (1) << "Attempting to create new character through move: " << cmd;

  const auto& factionVal = cmd["faction"];
  if (!factionVal.isString ())
    {
      LOG (WARNING) << "Character creation does not specify faction: " << cmd;
      return;
    }
  const Faction faction = FactionFromString (factionVal.asString ());
  if (faction == Faction::INVALID)
    {
      LOG (WARNING) << "Invalid faction specified for character: " << cmd;
      return;
    }

  if (cmd.size () != 1)
    {
      LOG (WARNING) << "Character creation has extra fields: " << cmd;
      return;
    }

  if (paidToDev < params.CharacterCost ())
    {
      LOG (WARNING) << "Required amount for new character not paid by " << name;
      return;
    }

  auto newChar = characters.CreateNew (name, faction);

  /* The starting position depends on the faction.  For initial testing,
     we start each faction a bit off the other (so there is no immediate combat)
     but not yet in the final way.  */
  switch (faction)
    {
    case Faction::RED:
      newChar->SetPosition (HexCoord (-1100, 942));
      break;

    case Faction::GREEN:
      newChar->SetPosition (HexCoord (-1042, 1165));
      break;

    case Faction::BLUE:
      newChar->SetPosition (HexCoord (-1377, 1163));
      break;

    default:
      LOG (FATAL) << "Invalid faction: " << static_cast<int> (faction);
    }

  /* For now, we just define some stats that every character has.  This will
     be changed later when the stats can be modified and are dynamic
     themselves.  */

  auto& pb = newChar->MutableProto ();
  auto* cd = pb.mutable_combat_data ();

  auto* attack = cd->add_attacks ();
  attack->set_range (10);
  attack->set_max_damage (1);
  attack = cd->add_attacks ();
  attack->set_range (1);
  attack->set_max_damage (5);

  auto* maxHP = cd->mutable_max_hp ();
  maxHP->set_armour (100);
  maxHP->set_shield (30);
  cd->set_shield_regeneration_mhp (500);

  newChar->MutableHP () = *maxHP;
}

namespace
{

/**
 * Transfers the given character if the update JSON contains a request
 * to do so.
 */
void
MaybeTransferCharacter (Character& c, const Json::Value& upd)
{
  CHECK (upd.isObject ());
  const auto& sendTo = upd["send"];
  if (!sendTo.isString ())
    return;

  VLOG (1)
      << "Sending character " << c.GetId ()
      << " from " << c.GetOwner ()
      << " to " << sendTo.asString ();
  c.SetOwner (sendTo.asString ());
}

/**
 * Processes a command to start prospecting at the character's current location.
 */
void
MaybeStartProspecting (Character& c, const Json::Value& upd,
                       RegionsTable& regions, const BaseMap& map)
{
  CHECK (upd.isObject ());
  const auto& cmd = upd["prospect"];
  if (!cmd.isObject ())
    return;

  if (!cmd.empty ())
    {
      LOG (WARNING)
          << "Invalid prospecting command for character " << c.GetId ()
          << ": " << cmd;
      return;
    }

  if (c.GetBusy () > 0)
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't prospect";
      return;
    }

  const auto& pos = c.GetPosition ();
  const auto regionId = map.Regions ().GetRegionId (pos);
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
      return;
    }
  if (rpb.has_prospection ())
    {
      LOG (WARNING)
          << "Region " << regionId
          << " is already prospected, can't be prospected by " << c.GetId ();
      return;
    }

  VLOG (1)
      << "Starting prospection of region " << regionId
      << " by character " << c.GetId ();

  r->MutableProto ().set_prospecting_character (c.GetId ());

  c.SetPartialStep (0);
  c.MutableProto ().clear_movement ();
  c.SetBusy (10);
  c.MutableProto ().mutable_prospection ();
}

/**
 * Sets the character's waypoints if a valid command for starting a move
 * is there.
 */
void
MaybeSetCharacterWaypoints (Character& c, const Json::Value& upd)
{
  CHECK (upd.isObject ());
  const auto& wpArr = upd["wp"];
  if (!wpArr.isArray ())
    return;

  if (c.GetBusy () > 0)
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " is busy, can't set waypoints";
      return;
    }

  std::vector<HexCoord> wp;
  for (const auto& entry : wpArr)
    {
      HexCoord coord;
      if (!CoordFromJson (entry, coord))
        {
          LOG (WARNING)
              << "Invalid waypoints given for character " << c.GetId ()
              << ", not updating movement";
          return;
        }
      wp.push_back (coord);
    }

  VLOG (1)
      << "Updating movement for character " << c.GetId ()
      << " from waypoints: " << wpArr;

  c.SetPartialStep (0);
  auto* mv = c.MutableProto ().mutable_movement ();
  mv->Clear ();
  SetRepeatedCoords (wp, *mv->mutable_waypoints ());
}

} // anonymous namespace

void
MoveProcessor::HandleCharacterUpdate (const std::string& name,
                                      const Json::Value& mv)
{
  const auto& cmd = mv["c"];
  if (!cmd.isObject ())
    return;

  for (auto i = cmd.begin (); i != cmd.end (); ++i)
    {
      Database::IdT id;
      if (!IdFromString (i.name (), id))
        {
          LOG (WARNING)
              << "Ignoring invalid character ID for update: " << i.name ();
          continue;
        }

      const auto& upd = *i;
      if (!upd.isObject ())
        {
          LOG (WARNING)
              << "Character update is not an object: " << upd;
          continue;
        }

      auto c = characters.GetById (id);
      if (c == nullptr)
        {
          LOG (WARNING)
              << "Character ID does not exist: " << id;
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

      MaybeTransferCharacter (*c, upd);
      MaybeStartProspecting (*c, upd, regions, map);
      MaybeSetCharacterWaypoints (*c, upd);
    }
}

} // namespace pxd
