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

#ifndef PXD_MOVEMENT_HPP
#define PXD_MOVEMENT_HPP

#include "context.hpp"
#include "dynobstacles.hpp"

#include "database/character.hpp"
#include "database/database.hpp"
#include "database/faction.hpp"
#include "mapdata/basemap.hpp"
#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

#include <functional>

namespace pxd
{

/**
 * Clears all movement for the given character (stops its movement entirely).
 */
void StopCharacter (Character& c);

/**
 * Handles movement of all characters from the given database.  This also
 * makes sure to update the dynamic obstacles, and "adds" them on top of
 * the given edge weights.
 */
void ProcessAllMovement (Database& db, DynObstacles& dyn, const Context& ctx);

/**
 * RAII helper class to remove a vehicle from the dynamic obstacles and
 * then add it back again at the (potentially) changed position.
 */
class MoveInDynObstacles
{

private:

  /** The character to move.  */
  const Character& character;

  /** Dynamic obstacles instance to update.  */
  DynObstacles& dyn;

public:

  MoveInDynObstacles () = delete;
  MoveInDynObstacles (const MoveInDynObstacles&) = delete;
  void operator= (const MoveInDynObstacles&) = delete;

  explicit MoveInDynObstacles (const Character& c, DynObstacles& d);
  ~MoveInDynObstacles ();

};

namespace test
{

/** Closure representing base-map edge weights.  */
using EdgeWeightFcn
    = std::function<PathFinder::DistanceT (const HexCoord& from,
                                           const HexCoord& to)>;

/**
 * Evaluates the edge-weight function based on the function of the basemap
 * and additionally excluding movement to locations where a dynamic obstacle is.
 */
PathFinder::DistanceT MovementEdgeWeight (
    const HexCoord& from, const HexCoord& to,
    const EdgeWeightFcn& baseEdges, const DynObstacles& dyn,
    Faction f);

/**
 * Processes movement (if any) for the given character handle and edge weights.
 */
void ProcessCharacterMovement (Character& c, const Context& ctx,
                               const EdgeWeightFcn& edges);

} // namespace test

} // namespace pxd

#endif // PXD_MOVEMENT_HPP
