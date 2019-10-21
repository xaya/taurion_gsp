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

#include "resourcedist.hpp"

namespace pxd
{

void
DetectResource (const HexCoord& pos, xaya::Random& rnd,
                std::string& type, Inventory::QuantityT& amount)
{
  /* FIXME: This is just some arbitrary logic for now so we can test the
     general prospecting and mining logic.  We need to replace this with
     a proper implementation based on how we want to distribute resources.  */

  const std::vector<std::string> types = {"raw a", "raw b"};
  const auto ind = rnd.SelectByWeight ({10, 1});

  type = types[ind];
  amount = 1 + rnd.NextInt (100);
}

} // namespace pxd
