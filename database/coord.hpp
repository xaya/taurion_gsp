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

#ifndef DATABASE_COORD_HPP
#define DATABASE_COORD_HPP

#include "database.hpp"

#include "hexagonal/coord.hpp"

#include <cstdint>

namespace pxd
{

/**
 * A database result that includes a x/y coordinate.
 */
struct ResultWithCoord : public Database::ResultType
{
  RESULT_COLUMN (int64_t, x, 51);
  RESULT_COLUMN (int64_t, y, 52);
};

/**
 * Retrieves a coordinate from a database column.
 *
 * This is templated, so that it can accept different database result types.
 * They should all be derived from ResultWithCoord, though.
 */
template <typename T>
  HexCoord GetCoordFromColumn (const Database::Result<T>& res);

/**
 * Binds a coordinate value to a pair of statement parameter.
 */
void BindCoordParameter (Database::Statement& stmt,
                         unsigned indX, unsigned indY,
                         const HexCoord& coord);

} // namespace pxd

#include "coord.tpp"

#endif // DATABASE_COORD_HPP
