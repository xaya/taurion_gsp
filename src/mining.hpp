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

#ifndef PXD_MINING_HPP
#define PXD_MINING_HPP

#include "context.hpp"

#include "database/database.hpp"

#include <xayautil/random.hpp>

namespace pxd
{

/**
 * Processes all mining of characters in the current turn.
 */
void ProcessAllMining (Database& db, xaya::Random& rnd, const Context& ctx);

} // namespace pxd

#endif // PXD_MINING_HPP
