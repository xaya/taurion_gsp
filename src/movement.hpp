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

#include <json/json.h>

#include <functional>

namespace pxd
{

/**
 * Encodes a list of hex coordinates (waypoints) into a compressed string
 * that is used for moves.  Returns true on success, and false if it failed.
 * This may be the case e.g. when the final size is too large for our
 * maximum uncompressed size.
 *
 * The format is to write out the waypoints as JSON array, serialise it,
 * compress is using libxayautil, and then base64 encode.  But that is
 * an implementation detail.
 */
bool EncodeWaypoints (const std::vector<HexCoord>& wp,
                      Json::Value& jsonWp, std::string& encoded);

/**
 * Tries to decode an encoded list of waypoints.  Returns true on success
 * and false if they were completely invalid (e.g. malformed).
 */
bool DecodeWaypoints (const std::string& encoded, std::vector<HexCoord>& wp);

/**
 * Computes the edge weight used for movement of a given faction character
 * on the map, not including dynamic obstacles.  This is shared between the
 * RPC server's findpath method and the actual GSP movement processing logic.
 */
inline PathFinder::DistanceT MovementEdgeWeight (const BaseMap& map, Faction f,
                                                 const HexCoord& from,
                                                 const HexCoord& to);

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
    const EdgeWeightFcn& baseEdges, const DynObstacles& dyn, Faction f,
    const HexCoord& from, const HexCoord& to);

/**
 * Processes movement (if any) for the given character handle and edge weights.
 */
void ProcessCharacterMovement (Character& c, const Context& ctx,
                               const EdgeWeightFcn& edges);

} // namespace test

} // namespace pxd

#include "movement.tpp"

#endif // PXD_MOVEMENT_HPP
