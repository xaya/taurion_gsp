/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#ifndef PXD_SPAWN_HPP
#define PXD_SPAWN_HPP

#include "context.hpp"
#include "dynobstacles.hpp"

#include "database/character.hpp"
#include "database/faction.hpp"
#include "mapdata/basemap.hpp"

#include <xayautil/random.hpp>

#include <string>

namespace pxd
{

/**
 * Chooses a location for spawning a of the given faction.
 * This places them randomly within the given radius around the centre,
 * displacing them as needed to find an accessible spot.
 * This is used for leaving buildings.
 */
HexCoord ChooseSpawnLocation (const HexCoord& centre, HexCoord::IntT radius,
                              const Faction f, xaya::Random& rnd,
                              const DynObstacles& dyn, const BaseMap& map);

/**
 * Spawns a new character in the world.  This takes care of initialising the
 * character accordingly and updating the database as needed.
 *
 * This function returns a handle to the newly created character.
 */
CharacterTable::Handle SpawnCharacter (const std::string& owner, Faction f,
                                       CharacterTable& tbl, DynObstacles& dyn,
                                       xaya::Random& rnd, const Context& ctx);

} // namespace pxd

#endif // PXD_SPAWN_HPP
