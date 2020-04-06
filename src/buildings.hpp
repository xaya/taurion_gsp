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

#ifndef PXD_BUILDINGS_HPP
#define PXD_BUILDINGS_HPP

#include "context.hpp"
#include "dynobstacles.hpp"

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/database.hpp"
#include "hexagonal/coord.hpp"
#include "proto/building.pb.h"

#include <xayautil/random.hpp>

#include <vector>

namespace pxd
{

/**
 * Returns all shape tiles of a given building, taking the centre and
 * its shape trafo into account.
 */
std::vector<HexCoord> GetBuildingShape (const Building& b);

/**
 * Checks if a building of the given type and rotation can be placed
 * at the given location.  The conditions are that no tile must be taken
 * already by another building or character, and that all tiles must be
 * of the same region.
 */
bool CanPlaceBuilding (const std::string& type,
                       const proto::ShapeTransformation& trafo,
                       const HexCoord& pos,
                       const DynObstacles& dyn, const Context& ctx);

/**
 * Places initial buildings (ancient and obelisks) onto the map.
 */
void InitialiseBuildings (Database& db);

/**
 * Computes and updates the stats of a building (e.g. combat data, HP) from
 * its type and other attributes.
 */
void UpdateBuildingStats (Building& b);

/**
 * Processes the updates (without any validation) for entering the given
 * building with the given character.
 */
void EnterBuilding (Character& c, const Building& b);

/**
 * Processes all characters that want to enter a building, and lets them in
 * if it is possible for them.
 */
void ProcessEnterBuildings (Database& db);

/**
 * Makes the given character leave the building it is currently in.
 */
void LeaveBuilding (BuildingsTable& buildings, Character& c,
                    xaya::Random& rnd, DynObstacles& dyn, const Context& ctx);

} // namespace pxd

#endif // PXD_BUILDINGS_HPP
