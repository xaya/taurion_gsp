#include "faction.hpp"

#include <glog/logging.h>

namespace pxd
{

std::string
FactionToString (const Faction f)
{
  switch (f)
    {
    case Faction::RED:
      return "r";
    case Faction::GREEN:
      return "g";
    case Faction::BLUE:
      return "b";
    default:
      LOG (FATAL) << "Invalid faction: " << static_cast<int> (f);
    }
}

Faction
FactionFromString (const std::string& str)
{
  /* The string mappings below are also used in parsing moves, so they
     are consensus critical!  */

  if (str == "r")
    return Faction::RED;
  if (str == "g")
    return Faction::GREEN;
  if (str == "b")
    return Faction::BLUE;

  LOG (WARNING) << "String is not a valid faction: " << str;
  return Faction::INVALID;
}

Faction
GetFactionFromColumn (const Database::Result& res, const std::string& col)
{
  const int val = res.Get<int> (col);
  CHECK (val >= 1 && val <= 3)
      << "Invalid faction value from database: " << val;
  return static_cast<Faction> (val);
}

void
BindFactionParameter (Database::Statement& stmt, const unsigned ind,
                      const Faction f)
{
  switch (f)
    {
    case Faction::RED:
    case Faction::GREEN:
    case Faction::BLUE:
      stmt.Bind (ind, static_cast<int> (f));
      return;
    default:
      LOG (FATAL)
          << "Binding invalid faction to parameter: " << static_cast<int> (f);
    }
}

} // namespace pxd
