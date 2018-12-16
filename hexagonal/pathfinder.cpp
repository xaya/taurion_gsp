#include "pathfinder.hpp"

#include <glog/logging.h>

#include <queue>

namespace pxd
{

constexpr PathFinder::DistanceT PathFinder::NO_CONNECTION;

namespace
{

/**
 * A hex coordinate plus the associated tentative distance.  These make up the
 * elements in the priority queue used with Dijkstra's algorithm.
 */
struct CoordWithDistance
{

  /** The hex coordinate this is all about.  */
  HexCoord coord;

  /** The tentative distance for it.  */
  PathFinder::DistanceT dist;

  /**
   * Simple constructor, so that we can use emplace.
   */
  inline explicit CoordWithDistance (const HexCoord& c,
                                     const PathFinder::DistanceT d)
    : coord(c), dist(d)
  {}

};

/**
 * Order the elements correctly, such that the "maximum" element (which is
 * the top of the priority queue) has the smallest tentative distance.
 */
bool
operator< (const CoordWithDistance& a, const CoordWithDistance& b)
{
  return b.dist < a.dist;
}

} // anonymous namespace

PathFinder::DistanceT
PathFinder::Compute (const HexCoord& source, const HexCoord::IntT l1Range)
{
  LOG (INFO) << "Starting Dijkstra's algorithm for PathFinder";

  /* For now, disallow calling this function multiple times on the same
     PathFinder.  There is no strong reason for why we cannot allow that,
     but if we did, then we would have to consider either keeping also the
     priority queue (so the algorithm can just be continued) persistent
     or it would redo all the previous work anyway.  */
  CHECK (distances.empty ())
      << "PathFinder allows only one Compute call for now";

  /* Check that the source is actually accessible from any of its neighbours.
     If it is not, then we would just spend the computations for the full
     l1Range for nothing.  Doing this check here makes sure that we can
     quickly return if the user clicked on an obstacle as target, for
     instance.

     For the target, we don't need this check specifically -- in case it is
     not accessible, Dijkstra's algorithm will just die out immediately.  */
  bool sourceAccessible = false;
  for (const auto& n : source.Neighbours ())
    if (edgeWeight (source, n) != NO_CONNECTION)
      {
        sourceAccessible = true;
        break;
      }
  if (!sourceAccessible)
    {
      LOG (INFO) << "Source tile is not accessible from anywhere";
      return NO_CONNECTION;
    }

  /* Run Dijkstra's algorithm with a std::priority_queue.  Since we cannot
     lower tentative distances of elements, we simply insert another copy
     instead (with a lower distance).  That works, but of course creates a
     slightly larger memory footprint.  Note, though, that the old elements
     will drop out eventually anyway, when the algorithm has progressed up to
     their original distance.  In the typical situation of similar distances
     in each direction (e.g. only obstacles and otherwise uniform travel
     speeds), this means that the elements will drop out soon or no
     lowering will be needed at all.  So it should be fine.

     The alternative would be to implement a custom heap that allows "bubbling
     up" of elements, as done for instance here:

       https://sourceforge.net/p/octave/level-set/ci/master/tree/src/Heap.tpp

     But that seems unnecessarily complex for little gain.  */

  std::priority_queue<CoordWithDistance> todo;
  std::unordered_map<HexCoord, DistanceT> tentativeDists;

  todo.emplace (target, 0);
  /* Since we will just pop that element as best one in the first iteration
     of the loop below, there is no need to insert also an element into
     tentativeDists for it.  */

  while (!todo.empty ())
    {
      const CoordWithDistance cur = todo.top ();
      todo.pop ();

      /* The element popped is either already finalised anyway, or it will
         be finalised now.  In both cases, we can remove it from the map
         of tentative distances to save memory.  */
      tentativeDists.erase (cur.coord);

      /* Check if we already have a distance entry for that coordinate.  This
         can happen if we popped out an "outdated copy" of an element that
         had its distance lowered.  */
      const auto mitCurDist = distances.find (cur.coord);
      if (mitCurDist != distances.end ())
        {
          CHECK (mitCurDist->second <= cur.dist);
          continue;
        }

      /* Insert the current element as a finalised distance.  */
      distances.emplace (cur.coord, cur.dist);

      /* If this was the source, we are done.  */
      if (cur.coord == source)
        {
          LOG (INFO) << "Found source in Dijkstra's, done";
          break;
        }

      /* Compute the L1 distance between the current element and the target.
         If this is smaller than l1Range, then all neighbours are guaranteed
         to be within range as well and we don't have to individually compute
         their distances.  */
      const HexCoord::IntT curL1Dist = HexCoord::DistanceL1 (cur.coord, target);

      /* Process all neighbours for Dijkstra's algorithm.  */
      for (const auto& n : cur.coord.Neighbours ())
        {
          if (curL1Dist >= l1Range)
            {
              const HexCoord::IntT newL1Dist = HexCoord::DistanceL1 (n, target);
              if (newL1Dist > l1Range)
                {
                  VLOG (1) << "Ignoring coordinate out of range";
                  continue;
                }
            }

          const DistanceT stepDist = edgeWeight (n, cur.coord);
          if (stepDist == NO_CONNECTION)
            continue;

          const DistanceT distViaCur = cur.dist + stepDist;

          const auto mitNewDist = distances.find (n);
          if (mitNewDist != distances.end ())
            {
              CHECK (mitNewDist->second <= distViaCur);
              continue;
            }

          const auto mitNewTentative = tentativeDists.find (n);
          if (mitNewTentative == tentativeDists.end ())
            {
              tentativeDists.emplace (n, distViaCur);
              todo.emplace (n, distViaCur);
            }
          else if (distViaCur < mitNewTentative->second)
            {
              mitNewTentative->second = distViaCur;
              todo.emplace (n, distViaCur);
            }
          /* Else the new path is not interesting, since we already have
             one that is at least as good.  */
        }
    }

  LOG (INFO)
      << "Dijkstra's algorithm finished, queue still has "
      << todo.size () << " elements left";

  const auto mitSourceDist = distances.find (source);
  if (mitSourceDist == distances.end ())
    return NO_CONNECTION;
  return mitSourceDist->second;
}

PathFinder::Stepper
PathFinder::StepPath (const HexCoord& source) const
{
  CHECK (distances.count (source) > 0)
      << "No path from the given source has been computed yet";
  return Stepper (*this, source);
}

PathFinder::DistanceT
PathFinder::Stepper::Next ()
{
  CHECK (HasMore ());

  const auto curDistIt = finder.distances.find (position);
  CHECK (curDistIt != finder.distances.end ());
  const DistanceT curDist = curDistIt->second;

  DistanceT bestDist = NO_CONNECTION;
  HexCoord bestNeighbour;
  for (const auto& n : position.Neighbours ())
    {
      const auto distIt = finder.distances.find (n);
      if (distIt == finder.distances.end ())
        continue;
      if (bestDist == NO_CONNECTION || distIt->second < bestDist)
        {
          bestDist = distIt->second;
          bestNeighbour = n;
        }
    }

  CHECK_NE (bestDist, NO_CONNECTION) << "No good neighbour found along path";
  CHECK_LE (bestDist, curDist);

  position = bestNeighbour;
  return curDist - bestDist;
}

} // namespace pxd
