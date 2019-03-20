#include "movement.hpp"

#include "protoutils.hpp"

#include "hexagonal/coord.hpp"

namespace pxd
{

namespace
{

constexpr bool PROCESSING_DONE = true;
constexpr bool CONTINUE_PROCESSING = false;

/**
 * Clears all movement for the given character (stops its movement entirely).
 */
void
StopCharacter (Character& c)
{
  VLOG (1) << "Stopping movement for " << c.GetId ();
  c.MutableProto ().clear_movement ();
  c.SetPartialStep (0);
}

/**
 * Try to step along the precomputed path of the given character.  Returns true
 * if movement is done for this character and this block and false if there
 * is potentially more processing (e.g. a second step or computing the
 * next path).
 */
bool
StepAlongPrecomputed (Character& c, const PathFinder::EdgeWeightFcn& edges)
{
  VLOG (1) << "We have a precomputed path, trying to step it...";

  const auto& mv = c.GetProto ().movement ();
  CHECK_GT (mv.waypoints_size (), 0);

  int stepInd;
  for (stepInd = 0; stepInd < mv.steps_size (); ++stepInd)
    if (CoordFromProto (mv.steps (stepInd)) == c.GetPosition ())
      {
        VLOG (1) << "Found position in step list at index " << stepInd;
        break;
      }
  CHECK_LT (stepInd, mv.steps_size ());

  HexCoord dest;
  bool finishedSteps;
  if (stepInd + 1 < mv.steps_size ())
    {
      dest = CoordFromProto (mv.steps (stepInd + 1));
      finishedSteps = false;
    }
  else
    {
      dest = CoordFromProto (mv.waypoints (0));
      finishedSteps = true;
    }
  const auto dist = edges (c.GetPosition (), dest);
  VLOG (1)
      << "Current step from " << c.GetPosition () << " to " << dest
      << ": distance " << dist;

  if (dist == PathFinder::NO_CONNECTION)
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is stepping into obstacle, stopping";
      StopCharacter (c);
      return PROCESSING_DONE;
    }

  if (dist > c.GetPartialStep ())
    {
      VLOG (1) << "Next step is too far, waiting for now";
      return PROCESSING_DONE;
    }

  VLOG (1) << "Performing this step now...";
  c.SetPartialStep (c.GetPartialStep () - dist);
  c.SetPosition (dest);

  if (finishedSteps)
    {
      VLOG (1) << "Reached first waypoint";

      auto* mutableMv = c.MutableProto ().mutable_movement ();
      mutableMv->clear_steps ();

      CHECK_EQ (c.GetPosition (), CoordFromProto (mv.waypoints (0)));
      auto* wp = mutableMv->mutable_waypoints ();
      wp->erase (wp->begin ());

      if (wp->empty ())
        {
          VLOG (1) << "Movement is finished";
          StopCharacter (c);
          return PROCESSING_DONE;
        }
    }

  return CONTINUE_PROCESSING;
}

/**
 * Precomputes the path steps to the next waypoint.  Returns true if the
 * processing is now done for this block and false if more needs to be done
 * (e.g. potentially already stepping along that segment).
 */
bool
PrecomputeNextSegment (Character& c, const Params& params,
                       const PathFinder::EdgeWeightFcn& edges)
{
  VLOG (1) << "Trying to precompute path to the next waypoint...";

  const auto& mv = c.GetProto ().movement ();
  CHECK_EQ (mv.steps_size (), 0);
  CHECK_GT (mv.waypoints_size (), 0);

  const HexCoord pos = c.GetPosition ();
  const HexCoord wp = CoordFromProto (mv.waypoints (0));
  VLOG (1) << "That path will be from " << pos << " to " << wp;

  if (pos == wp)
    {
      /* We have to handle this special case here rather than computing an
         empty path and letting path stepping handle it since that always
         assumes that we step to a neighbour tile.  Thus it won't work
         correctly if the "step" would be zero-distance to the same tile.  */

      LOG (WARNING)
          << "Next waypoint equals current position of " << c.GetId ();

      auto* wp = c.MutableProto ().mutable_movement ()->mutable_waypoints ();
      wp->erase (wp->begin ());

      if (wp->empty ())
        {
          VLOG (1) << "No more waypoints";
          StopCharacter (c);
          return PROCESSING_DONE;
        }

      return CONTINUE_PROCESSING;
    }

  PathFinder finder(edges, wp);
  const auto dist = finder.Compute (pos, params.MaximumWaypointL1Distance ());
  VLOG (1) << "Shortest path has length " << dist;

  if (dist == PathFinder::NO_CONNECTION)
    {
      LOG (WARNING)
          << "Character " << c.GetId () << " cannot reach next waypoint "
          << wp << " from current position " << pos;
      StopCharacter (c);
      return PROCESSING_DONE;
    }

  auto path = finder.StepPath (pos);
  CHECK_EQ (path.GetPosition (), pos);
  CHECK (path.HasMore ());
  auto* stepsPb = c.MutableProto ().mutable_movement ()->mutable_steps ();
  while (true)
    {
      *stepsPb->Add () = CoordToProto (path.GetPosition ());
      path.Next ();
      if (!path.HasMore ())
        {
          CHECK_EQ (path.GetPosition (), wp);
          break;
        }
    }
  VLOG (1) << "Precomputed path with " << stepsPb->size () << " steps";

  return CONTINUE_PROCESSING;
}

} // anonymous namespace

void
ProcessCharacterMovement (Character& c, const Params& params,
                          const PathFinder::EdgeWeightFcn& edges)
{
  const auto speed = c.GetProto ().speed ();

  VLOG (1)
      << "Processing movement for character: " << c.GetId ()
      << " (speed: " << speed << ")";
  CHECK (c.GetProto ().has_movement ())
      << "Character " << c.GetId ()
      << " was selected for movement but is not actually moving";

  c.SetPartialStep (c.GetPartialStep () + speed);
  VLOG (1)
      << "Accumulated movement points for this step: " << c.GetPartialStep ();

  while (true)
    {
      const auto& mv = c.GetProto ().movement ();
      CHECK_GT (mv.waypoints_size (), 0)
          << "Character " << c.GetId ()
          << " has active movement but no waypoitns";

      /* If we have a precomputed path, try to do one step along it.  */
      if (mv.steps_size () > 0)
        {
          if (StepAlongPrecomputed (c, edges) == PROCESSING_DONE)
            break;
          continue;
        }

      /* Else, we need to precompute the next segment of the path.  */
      if (PrecomputeNextSegment (c, params, edges) == PROCESSING_DONE)
        break;
    }
}

void
ProcessAllMovement (Database& db, const Params& params,
                    const PathFinder::EdgeWeightFcn& edges)
{
  CharacterTable tbl(db);
  auto res = tbl.QueryMoving ();
  while (res.Step ())
    {
      const auto h = tbl.GetFromResult (res);
      ProcessCharacterMovement (*h, params, edges);
    }
}

} // namespace pxd
