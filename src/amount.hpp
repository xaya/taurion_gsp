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

#ifndef PXD_AMOUNT_HPP
#define PXD_AMOUNT_HPP

#include <cstdint>

namespace pxd
{

/** An amount of CHI, represented as Satoshi.  */
using Amount = int64_t;

/** Amount of one CHI.  */
constexpr Amount COIN = 100000000;

/**
 * Highest valid value for an amount.  This is just used for sanity checks
 * and not the precise money supply of CHI.
 */
constexpr Amount MAX_AMOUNT = 80000000 * COIN;

} // namespace pxd

#endif // PXD_AMOUNT_HPP
