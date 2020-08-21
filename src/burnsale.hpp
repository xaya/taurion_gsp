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

#ifndef PXD_BURNSALE_HPP
#define PXD_BURNSALE_HPP

#include "context.hpp"

#include "database/amount.hpp"

namespace pxd
{

/**
 * Computes how much vCHI will be bought with a CHI burn of the given amount
 * (and how much of the burnt CHI will actually be used by that).  This
 * implements the burnsale schedule / stages.
 *
 * The amount of vCHI sold in previous burns (from MoneySupply) has to
 * be passed in.
 *
 * Returned is the number of vCHI bought (if any).  The burnt CHI amount
 * is decremented by whatever is used up for that.
 */
Amount ComputeBurnsaleAmount (Amount& burntChi, Amount soldBefore,
                              const Context& ctx);

} // namespace pxd

#endif // PXD_BURNSALE_HPP
