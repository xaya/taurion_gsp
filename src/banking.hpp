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

#ifndef PXD_BANKING_HPP
#define PXD_BANKING_HPP

#include "context.hpp"

#include "database/database.hpp"

namespace pxd
{

/**
 * Processes all updates due to banking.  In other words, bankings the inventory
 * of all characters inside a banking area, and also updates their "points"
 * for completed resource sets.
 */
void ProcessBanking (Database& db, const Context& ctx);

} // namespace pxd

#endif // PXD_BANKING_HPP
