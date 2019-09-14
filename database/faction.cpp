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

void
BindFactionParameter (Database::Statement& stmt, const unsigned ind,
                      const Faction f)
{
  switch (f)
    {
    case Faction::RED:
    case Faction::GREEN:
    case Faction::BLUE:
      stmt.Bind (ind, static_cast<int64_t> (f));
      return;
    default:
      LOG (FATAL)
          << "Binding invalid faction to parameter: " << static_cast<int> (f);
    }
}

} // namespace pxd
