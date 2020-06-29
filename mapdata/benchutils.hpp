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

#ifndef MAPDATA_BENCHUTILS_HPP
#define MAPDATA_BENCHUTILS_HPP

#include "hexagonal/coord.hpp"

#include <cstddef>
#include <vector>

namespace pxd
{

/**
 * Returns a hex coordinate on the map chosen (mostly) randomly.
 */
HexCoord RandomCoord ();

/**
 * Constructs a vector of n "random" coordinates.
 */
std::vector<HexCoord> RandomCoords (size_t n);

} // namespace pxd

#endif // MAPDATA_BENCHUTILS_HPP
