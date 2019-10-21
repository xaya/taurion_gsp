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

#ifndef PXD_RESOURCEDIST_HPP
#define PXD_RESOURCEDIST_HPP

#include "hexagonal/coord.hpp"
#include "database/inventory.hpp"

#include <xayautil/random.hpp>

namespace pxd
{

/**
 * Determines the type and initial amount of resource mine-able that should
 * be found by prospecting in the given coordinate.
 */
void DetectResource (const HexCoord& pos, xaya::Random& rnd,
                     std::string& type, Inventory::QuantityT& amount);

} // namespace pxd

#endif // PXD_RESOURCEDIST_HPP
