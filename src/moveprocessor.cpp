#include "moveprocessor.hpp"

#include "jsonutils.hpp"

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

  Character newChar(db, name, charName);
  newChar.SetPosition (HexCoord (0, 0));
}

} // namespace pxd
