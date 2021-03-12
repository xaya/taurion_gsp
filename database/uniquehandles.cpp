/*
    GSP for the Taurion blockchain game
    Copyright (C) 2021  Autonomous Worlds Ltd

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

#include "uniquehandles.hpp"

#include <glog/logging.h>

namespace pxd
{

UniqueHandles::~UniqueHandles ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (active.empty ()) << active.size () << " handles are still active";
}

void
UniqueHandles::Add (const std::string& type, const std::string& id)
{
  std::lock_guard<std::mutex> lock(mut);
  const auto ins = active.emplace (type, id);
  CHECK (ins.second)
      << "Handle (" << type << ", " << id << ") is already active";
}

void
UniqueHandles::Remove (const std::string& type, const std::string& id)
{
  std::lock_guard<std::mutex> lock(mut);
  const auto mit = active.find (std::make_pair (type, id));
  CHECK (mit != active.end ())
      << "Handle (" << type << ", " << id << ") is not active";
  active.erase (mit);
}

UniqueHandles::Tracker::~Tracker ()
{
  handles.Remove (type, id);
}

} // namespace pxd
