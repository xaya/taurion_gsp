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

#ifndef PXD_ONGOINGS_HPP
#define PXD_ONGOINGS_HPP

#include "context.hpp"

#include "database/database.hpp"

#include <xayautil/random.hpp>

namespace pxd
{

/**
 * Processes ongoing operations (i.e. check which have reached the block height,
 * handle them, and then delete the ones that are done).
 */
void ProcessAllOngoings (Database& db, xaya::Random& rnd, const Context& ctx);

} // namespace pxd

#endif // PXD_ONGOINGS_HPP
