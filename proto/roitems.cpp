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

#include "roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

const proto::ItemData*
RoItemDataOrNull (const std::string& item)
{
  const auto& data = RoConfigData ().fungible_items ();
  const auto mit = data.find (item);
  if (mit != data.end ())
    return &mit->second;
  return nullptr;
}

const proto::ItemData&
RoItemData (const std::string& item)
{
  const auto* ptr = RoItemDataOrNull (item);
  CHECK (ptr != nullptr) << "Unknown item: " << item;
  return *ptr;
}

} // namespace pxd
