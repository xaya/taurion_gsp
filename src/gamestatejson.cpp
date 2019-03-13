#include "gamestatejson.hpp"

#include "jsonutils.hpp"
#include "protoutils.hpp"

#include "database/character.hpp"
#include "database/faction.hpp"
#include "hexagonal/pathfinder.hpp"
#include "proto/character.pb.h"

namespace pxd
{

namespace
{

/**
 * Converts a TargetId proto to its JSON gamestate form.
 */
Json::Value
TargetIdToJson (const proto::TargetId& target)
{
  Json::Value res(Json::objectValue);
  res["id"] = IntToJson (target.id ());

  switch (target.type ())
    {
    case proto::TargetId::TYPE_CHARACTER:
      res["type"] = "character";
      break;
    case proto::TargetId::TYPE_BUILDING:
      res["type"] = "building";
      break;
    default:
      LOG (FATAL) << "Invalid target type: " << target.type ();
    }

  return res;
}

/**
 * Converts an HP proto to a JSON form.
 */
Json::Value
HpProtoToJson (const proto::HP& hp)
{
  Json::Value res(Json::objectValue);
  res["armour"] = IntToJson (hp.armour ());

  const int baseShield = hp.shield ();
  if (hp.shield_mhp () == 0)
    res["shield"] = baseShield;
  else
    res["shield"] = baseShield + hp.shield_mhp () / 1000.0;

  return res;
}

/**
 * Computes the "combat" sub-object for a Character's JSON state.
 */
Json::Value
GetCombatJsonObject (const Character& c)
{
  Json::Value res(Json::objectValue);

  const auto& pb = c.GetProto ();
  if (pb.has_target ())
    res["target"] = TargetIdToJson (pb.target ());

  Json::Value attacks(Json::arrayValue);
  for (const auto& attack : pb.combat_data ().attacks ())
    {
      Json::Value obj(Json::objectValue);
      obj["range"] = IntToJson (attack.range ());
      obj["maxdamage"] = IntToJson (attack.max_damage ());
      attacks.append (obj);
    }
  if (!attacks.empty ())
    res["attacks"] = attacks;

  Json::Value hp(Json::objectValue);
  hp["max"] = HpProtoToJson (pb.combat_data ().max_hp ());
  hp["current"] = HpProtoToJson (c.GetHP ());
  hp["regeneration"] = pb.combat_data ().shield_regeneration_mhp () / 1000.0;
  res["hp"] = hp;

  return res;
}

} // anonymous namespace

template <>
  Json::Value
  ToStateJson<Character> (const Character& c)
{
  const auto& pb = c.GetProto ();

  Json::Value res(Json::objectValue);
  res["id"] = IntToJson (c.GetId ());
  res["owner"] = c.GetOwner ();
  res["faction"] = FactionToString (c.GetFaction ());
  res["position"] = CoordToJson (c.GetPosition ());
  res["combat"] = GetCombatJsonObject (c);

  Json::Value mv(Json::objectValue);
  if (c.GetPartialStep () != 0)
    mv["partialstep"] = IntToJson (c.GetPartialStep ());
  if (pb.has_movement ())
    {
      const auto& mvProto = pb.movement ();

      Json::Value wp(Json::arrayValue);
      for (const auto& entry : mvProto.waypoints ())
        wp.append (CoordToJson (CoordFromProto (entry)));
      if (wp.size () > 0)
        mv["waypoints"] = wp;

      /* The precomputed path is processed (rather than just translated from
         proto to JSON):  We strip off already visited points from it, and
         we "shift" it by one so that the points represent destinations
         and it is easier to understand.  */
      Json::Value path(Json::arrayValue);
      bool foundPosition = false;
      for (const auto& s : mvProto.steps ())
        {
          const HexCoord from = CoordFromProto (s);
          if (from == c.GetPosition ())
            {
              CHECK (!foundPosition);
              foundPosition = true;
            }
          else if (foundPosition)
            path.append (CoordToJson (from));
        }
      CHECK (foundPosition || mvProto.steps_size () == 0);
      if (foundPosition)
        {
          CHECK (wp.size () > 0);
          path.append (wp[0]);
          mv["steps"] = path;
        }
    }
  if (!mv.empty ())
    res["movement"] = mv;

  return res;
}

namespace
{

/**
 * Extracts all results from the Database::Result instance, converts them
 * to JSON, and returns a JSON array.
 */
template <typename T>
  Json::Value
  ResultsAsArray (T& tbl, Database::Result res)
{
  Json::Value arr(Json::arrayValue);

  while (res.Step ())
    {
      const auto h = tbl.GetFromResult (res);
      arr.append (ToStateJson (*h));
    }

  return arr;
}

} // anonymous namespace

Json::Value
GameStateToJson (Database& db)
{
  Json::Value res(Json::objectValue);

  {
    CharacterTable tbl(db);
    res["characters"] = ResultsAsArray (tbl, tbl.QueryAll ());
  }

  return res;
}

} // namespace pxd
