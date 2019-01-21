#include "moveprocessor.hpp"

#include "jsonutils.hpp"
#include "protoutils.hpp"

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

  const auto& charNameVal = cmd["name"];
  if (!charNameVal.isString ())
    {
      LOG (WARNING) << "Character creation does not specify name: " << cmd;
      return;
    }
  const std::string charName = charNameVal.asString ();

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

  if (!characters.IsValidName (charName))
    {
      LOG (WARNING) << "Invalid character name: " << charName;
      return;
    }

  auto newChar = characters.CreateNew (name, charName);
  newChar->SetPosition (HexCoord (0, 0));
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
      << " (name: " << c.GetName () << ") from " << c.GetOwner ()
      << " to " << sendTo.asString ();
  c.SetOwner (sendTo.asString ());
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

  std::vector<HexCoord> wp;
  for (const auto& entry : wpArr)
    {
      HexCoord coord;
      if (!CoordFromJson (entry, coord))
        {
          LOG (WARNING)
              << "Invalid waypoints given for " << c.GetName ()
              << ", not updating movement";
          return;
        }
      wp.push_back (coord);
    }

  VLOG (1)
      << "Updating movement for character " << c.GetName ()
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
      MaybeSetCharacterWaypoints (*c, upd);
    }
}

} // namespace pxd
