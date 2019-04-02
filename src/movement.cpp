#include "movement.hpp"

#include "protoutils.hpp"

namespace pxd
{

void
StopCharacter (Character& c)
{
  VLOG (1) << "Stopping movement for " << c.GetId ();
  c.MutableProto ().clear_movement ();
  c.MutableVolatileMv ().Clear ();
}

namespace
{

constexpr bool PROCESSING_DONE = true;
constexpr bool CONTINUE_PROCESSING = false;

/**
 * Try to step along the precomputed path of the given character.  Returns true
 * if movement is done for this character and this block and false if there
 * is potentially more processing (e.g. a second step or computing the
 * next path).
 */
bool
StepAlongPrecomputed (Character& c, const Params& params,
                      const PathFinder::EdgeWeightFcn& edges)
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
  CHECK_EQ (HexCoord::DistanceL1 (c.GetPosition (), dest), 1);
  const auto dist = edges (c.GetPosition (), dest);
  VLOG (1)
      << "Current step from " << c.GetPosition () << " to " << dest
      << ": distance " << dist;

  if (dist == PathFinder::NO_CONNECTION)
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is stepping into obstacle from "
          << c.GetPosition () << " to " << dest;

      /* When the step is blocked, we set all partial steps to zero and stop
         processing for now.  However, we keep retrying that step a couple of
         times, in case it is just a passing vehicle and movement will be
         free again later.  But if the way is still blocked after some time,
         we stop movement completely to avoid trying forever.  */

      auto& volMv = c.MutableVolatileMv ();
      volMv.clear_partial_step ();
      volMv.set_blocked_turns (volMv.blocked_turns () + 1);
      VLOG (1)
          << "Incremented blocked turns counter to " << volMv.blocked_turns ();

      if (volMv.blocked_turns () > params.BlockedStepRetries ())
        {
          VLOG (1)
              << "Too many blocked turns, stopping character " << c.GetId ();
          StopCharacter (c);
        }

      return PROCESSING_DONE;
    }

  /* If the way is free (independent of whether or not we can step there),
     reset the blocked turns counter to zero.  */
  const auto& volMv = c.GetVolatileMv ();
  if (volMv.has_blocked_turns ())
    {
      VLOG (1)
          << "Clearing blocked turns counter (old value: "
          << volMv.blocked_turns () << ")";
      c.MutableVolatileMv ().clear_blocked_turns ();
    }

  if (dist > volMv.partial_step ())
    {
      VLOG (1) << "Next step is too far, waiting for now";
      return PROCESSING_DONE;
    }

  VLOG (1) << "Performing this step now...";
  c.MutableVolatileMv ().set_partial_step (volMv.partial_step () - dist);
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

  const unsigned partialStep = c.GetVolatileMv ().partial_step () + speed;
  c.MutableVolatileMv ().set_partial_step (partialStep);
  VLOG (1)
      << "Accumulated movement points for this step: " << partialStep;

  while (true)
    {
      const auto& mv = c.GetProto ().movement ();
      CHECK_GT (mv.waypoints_size (), 0)
          << "Character " << c.GetId ()
          << " has active movement but no waypoitns";

      /* If we have a precomputed path, try to do one step along it.  */
      if (mv.steps_size () > 0)
        {
          if (StepAlongPrecomputed (c, params, edges) == PROCESSING_DONE)
            break;
          continue;
        }

      /* Else, we need to precompute the next segment of the path.  */
      if (PrecomputeNextSegment (c, params, edges) == PROCESSING_DONE)
        break;
    }
}

PathFinder::DistanceT
MovementEdgeWeight (const HexCoord& from, const HexCoord& to,
                    const PathFinder::EdgeWeightFcn& baseEdges,
                    const DynObstacles& dyn,
                    const Faction f)
{
  /* With dynamic obstacles, we do not handle the situation well if from and
     to are the same location.  In that case, the vehicle itself will be
     seen as obstacle (which it should not).  */
  CHECK_NE (from, to);

  const auto res = baseEdges (from, to);

  if (res == PathFinder::NO_CONNECTION
        || !dyn.IsPassable (from, f) || !dyn.IsPassable (to, f))
    return PathFinder::NO_CONNECTION;

  return res;
}

namespace
{

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

  explicit MoveInDynObstacles (const Character& c, DynObstacles& d)
    : character(c), dyn(d)
  {
    VLOG (1)
        << "Removing character " << character.GetId ()
        << " at position " << character.GetPosition ()
        << " from the dynamic obstacle map before moving it...";
    dyn.RemoveVehicle (character.GetPosition (), character.GetFaction ());
  }

  ~MoveInDynObstacles ()
  {
    VLOG (1)
        << "Adding back character " << character.GetId ()
        << " at position " << character.GetPosition ()
        << " to the dynamic obstacle map...";
    dyn.AddVehicle (character.GetPosition (), character.GetFaction ());
  }

};

} // anonymous namespace

void
ProcessAllMovement (Database& db, DynObstacles& dyn, const Params& params,
                    const PathFinder::EdgeWeightFcn& baseEdges)
{
  CharacterTable tbl(db);
  auto res = tbl.QueryMoving ();
  while (res.Step ())
    {
      const auto c = tbl.GetFromResult (res);
      MoveInDynObstacles dynMover(*c, dyn);

      const Faction f = c->GetFaction ();
      const auto edges = [&baseEdges, &dyn, f] (const HexCoord& from,
                                                const HexCoord& to)
        {
          return MovementEdgeWeight (from, to, baseEdges, dyn, f);
        };
      ProcessCharacterMovement (*c, params, edges);
    }
}

} // namespace pxd
