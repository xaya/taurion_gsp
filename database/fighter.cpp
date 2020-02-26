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

#include "fighter.hpp"

#include <glog/logging.h>

namespace pxd
{

FighterTable::Handle
FighterTable::GetForTarget (const proto::TargetId& id)
{
  switch (id.type ())
    {
    case proto::TargetId::TYPE_BUILDING:
      return buildings.GetById (id.id ());

    case proto::TargetId::TYPE_CHARACTER:
      return characters.GetById (id.id ());

    default:
      LOG (FATAL) << "Invalid target type: " << static_cast<int> (id.type ());
    }
}

void
FighterTable::ProcessWithAttacks (const Callback& cb)
{
  {
    auto res = buildings.QueryWithAttacks ();
    while (res.Step ())
      cb (buildings.GetFromResult (res));
  }

  {
    auto res = characters.QueryWithAttacks ();
    while (res.Step ())
      cb (characters.GetFromResult (res));
  }
}

void
FighterTable::ProcessForRegen (const Callback& cb)
{
  {
    auto res = buildings.QueryForRegen ();
    while (res.Step ())
      cb (buildings.GetFromResult (res));
  }

  {
    auto res = characters.QueryForRegen ();
    while (res.Step ())
      cb (characters.GetFromResult (res));
  }
}

void
FighterTable::ProcessWithTarget (const Callback& cb)
{
  {
    auto res = buildings.QueryWithTarget ();
    while (res.Step ())
      cb (buildings.GetFromResult (res));
  }

  {
    auto res = characters.QueryWithTarget ();
    while (res.Step ())
      cb (characters.GetFromResult (res));
  }
}

} // namespace pxd
