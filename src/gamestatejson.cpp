#include "gamestatejson.hpp"

#include "jsonutils.hpp"

namespace pxd
{

Json::Value
CharacterToJson (const Character& c)
{
  Json::Value res(Json::objectValue);
  res["id"] = static_cast<int> (c.GetId ());
  res["owner"] = c.GetOwner ();
  res["name"] = c.GetName ();
  res["position"] = CoordToJson (c.GetPosition ());
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
