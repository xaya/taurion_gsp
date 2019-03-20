#ifndef PXD_MOVEMENT_HPP
#define PXD_MOVEMENT_HPP

#include "params.hpp"

#include "database/character.hpp"
#include "database/database.hpp"
#include "hexagonal/pathfinder.hpp"

namespace pxd
{

/**
 * Processes movement (if any) for the given character handle and edge weights.
 */
void ProcessCharacterMovement (Character& c, const Params& params,
                               const PathFinder::EdgeWeightFcn& edges);

/**
 * Handles movement of all characters from the given database.
 */
void ProcessAllMovement (Database& db, const Params& params,
                         const PathFinder::EdgeWeightFcn& edges);

} // namespace pxd

#endif // PXD_MOVEMENT_HPP
