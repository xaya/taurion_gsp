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

#include "modifier.hpp"

namespace pxd
{

proto::StatModifier
StatModifier::ToProto () const
{
  proto::StatModifier res;

  if (percent != 0)
    res.set_percent (percent);

  if (absolute != 0)
    res.set_absolute (absolute);

  return res;
}

proto::StatModifier&
operator+= (proto::StatModifier& pb, const proto::StatModifier& other)
{
  StatModifier mod(pb);
  mod += other;

  pb = mod.ToProto ();
  return pb;
}

} // namespace pxd
