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

#include "benchutils.hpp"

#include "tiledata.hpp"

#include <cstdlib>

namespace pxd
{

HexCoord
RandomCoord ()
{
  using namespace tiledata;

  const int y = minY + std::rand () % (maxY - minY + 1);
  const int yInd = y - minY;
  const int x = minX[yInd] + std::rand () % (maxX[yInd] - minX[yInd] + 1);

  return HexCoord (x, y);
}

std::vector<HexCoord>
RandomCoords (const size_t n)
{
  std::vector<HexCoord> res;
  res.reserve (n);

  for (unsigned i = 0; i < n; ++i)
    res.push_back (RandomCoord ());

  return res;
}

} // namespace pxd
