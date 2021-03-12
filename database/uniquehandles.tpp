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

/* Template implementation for uniquehandles.hpp.  */

#include <sstream>

namespace pxd
{

template <typename T>
  UniqueHandles::Tracker::Tracker (UniqueHandles& h,
                                   const std::string& t, const T& i)
    : handles(h), type(t)
{
  std::ostringstream out;
  out << i;
  id = out.str ();

  handles.Add (type, id);
}

} // namespace pxd
