/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#include "pathfinder.hpp"

namespace pxd
{

constexpr PathFinder::DistanceT PathFinder::NO_CONNECTION;

PathFinder::Stepper
PathFinder::StepPath (const HexCoord& source) const
{
  CHECK (distances != nullptr && distances->Get (source) != NO_CONNECTION)
      << "No path from the given source has been computed yet";
  return Stepper (*this, source);
}

bool
PathFinder::Stepper::TryStep (const HexCoord& target, DistanceT& step)
{
  const auto curDist = finder.distances->Get (position);
  CHECK (curDist != NO_CONNECTION);

  if (!finder.distances->IsInRange (target))
    return false;
  const auto dist = finder.distances->Get (target);
  if (dist == NO_CONNECTION)
    return false;

  step = finder.edges (position, target);
  if (step == NO_CONNECTION)
    return false;
  const auto fullDist = dist + step;

  if (fullDist == curDist)
    {
      lastDirection = target - position;
      position = target;
      return true;
    }

  CHECK_GT (fullDist, curDist);
  return false;
}

PathFinder::DistanceT
PathFinder::Stepper::Next ()
{
  CHECK (HasMore ());

  /* We try to continue the last direction, as long as it is an optimal
     path.  This helps to avoid spurious turns when moving around obstacles,
     and minimises (at least in a greedy way) the number of waypoints needed
     for the final path.  */
  DistanceT step;
  if (lastDirection != HexCoord::Difference ())
    {
      const auto continued = position + lastDirection;
      if (TryStep (continued, step))
        return step;
    }

  for (const auto& n : position.Neighbours ())
    if (TryStep (n, step))
      return step;

  LOG (FATAL) << "No good neighbour found along path";
}

} // namespace pxd
