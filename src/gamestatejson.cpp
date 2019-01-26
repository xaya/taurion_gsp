#include "gamestatejson.hpp"

#include "jsonutils.hpp"
#include "protoutils.hpp"

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
  res["id"] = static_cast<int> (target.id ());

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

} // anonymous namespace

Json::Value
CharacterToJson (const Character& c)
{
  const auto& pb = c.GetProto ();

  Json::Value res(Json::objectValue);
  res["id"] = static_cast<int> (c.GetId ());
  res["owner"] = c.GetOwner ();
  res["name"] = c.GetName ();
  res["faction"] = FactionToString (c.GetFaction ());
  res["position"] = CoordToJson (c.GetPosition ());

  Json::Value mv(Json::objectValue);
  if (c.GetPartialStep () != 0)
    mv["partialstep"] = static_cast<int> (c.GetPartialStep ());
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
  if (mv.size () > 0)
    res["movement"] = mv;

  if (pb.has_target ())
    res["target"] = TargetIdToJson (pb.target ());

  return res;
}

Json::Value
GameStateToJson (Database& db)
{
  Json::Value res(Json::objectValue);

  {
    Json::Value chars(Json::arrayValue);

    CharacterTable tbl(db);
    auto dbRes = tbl.QueryAll ();
    while (dbRes.Step ())
      {
        const auto c = tbl.GetFromResult (dbRes);
        chars.append (CharacterToJson (*c));
      }

    res["characters"] = chars;
  }

  return res;
}

} // namespace pxd
