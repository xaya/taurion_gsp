/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

/* Inline code for safezones.hpp.  */

#include "dyntiles.hpp"

#include <glog/logging.h>

namespace pxd
{

void
SafeZones::GetPosition (const HexCoord& c, size_t& ind, unsigned& shift)
{
  const size_t fullIndex = dyntiles::GetIndex (c);
  ind = fullIndex / 2;
  shift = (fullIndex % 2 == 0 ? 0 : 4);
}

SafeZones::Entry
SafeZones::GetEntry (const HexCoord& c) const
{
  size_t ind;
  unsigned shift;
  GetPosition (c, ind, shift);

  return static_cast<Entry> ((data[ind] >> shift) & 0x0F);
}

bool
SafeZones::IsNoCombat (const HexCoord& c) const
{
  const Entry e = GetEntry (c);
  switch (e)
    {
    case Entry::NONE:
      return false;
    case Entry::RED:
    case Entry::GREEN:
    case Entry::BLUE:
    case Entry::NEUTRAL:
      return true;
    default:
      LOG (FATAL) << "Invalid data entry: " << static_cast<int> (e);
    }
}

Faction
SafeZones::StarterFor (const HexCoord& c) const
{
  const Entry e = GetEntry (c);
  switch (e)
    {
    case Entry::NONE:
    case Entry::NEUTRAL:
      return Faction::INVALID;
    case Entry::RED:
    case Entry::GREEN:
    case Entry::BLUE:
      return static_cast<Faction> (e);
    default:
      LOG (FATAL) << "Invalid data entry: " << static_cast<int> (e);
    }
}

} // namespace pwd
