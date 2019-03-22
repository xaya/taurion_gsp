#ifndef PXD_MOVEMENT_HPP
#define PXD_MOVEMENT_HPP

#include "dynobstacles.hpp"
#include "params.hpp"

#include "database/character.hpp"
#include "database/database.hpp"
#include "database/faction.hpp"
#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

namespace pxd
{

/**
 * Processes movement (if any) for the given character handle and edge weights.
 */
void ProcessCharacterMovement (Character& c, const Params& params,
                               const PathFinder::EdgeWeightFcn& edges);

/**
 * Evaluates the edge-weight function based on the function of the basemap
 * and additionally excluding movement to locations where a dynamic obstacle is.
 */
PathFinder::DistanceT MovementEdgeWeight (
    const HexCoord& from, const HexCoord& to,
    const PathFinder::EdgeWeightFcn& baseEdges, const DynObstacles& dyn,
    Faction f);

/**
 * Handles movement of all characters from the given database.  This also
 * makes sure to update the dynamic obstacles, and "adds" them on top of
 * the given edge weights.
 */
void ProcessAllMovement (Database& db, DynObstacles& dyn, const Params& params,
                         const PathFinder::EdgeWeightFcn& baseEdges);

} // namespace pxd

#endif // PXD_MOVEMENT_HPP
