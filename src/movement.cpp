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

#include "movement.hpp"

#include "jsonutils.hpp"
#include "modifier.hpp"
#include "protoutils.hpp"

#include <xayautil/compression.hpp>

#include <glog/logging.h>

namespace pxd
{

/* ************************************************************************** */

namespace
{

/**
 * The maximum size of uncompressed serialised waypoints.  This is used when
 * uncompressing data using libxayautil to ensure there is no DDoS attack
 * vector on memory (zip bomb).  It is consensus relevant, as it may mean some
 * waypoint moves are invalid.  The number is so high that it should not matter
 * in practice, though.  "Normal paths" moving all across the map are only
 * about 3-4 KiB in size.
 */
constexpr size_t MAX_WAYPOINT_SIZE = (1 << 20);

} // anonymous namespace

bool
EncodeWaypoints (const std::vector<HexCoord>& wp,
                 Json::Value& jsonWp, std::string& encoded)
{
  jsonWp = Json::Value (Json::arrayValue);
  for (const auto& c : wp)
    jsonWp.append (CoordToJson (c));

  std::string serialised;
  CHECK (xaya::CompressJson (jsonWp, encoded, serialised));
  if (serialised.size () > MAX_WAYPOINT_SIZE)
    {
      LOG (WARNING)
          << "Serialised waypoints JSON is too large (" << serialised.size ()
          << " vs maximum allowed length " << MAX_WAYPOINT_SIZE << ")";
      return false;
    }

  VLOG (1)
      << "Encoded " << wp.size () << " waypoints;"
      << " the serialised size is " << serialised.size ()
      << ", the encoded size is " << encoded.size ();

  return true;
}

bool
DecodeWaypoints (const std::string& encoded, std::vector<HexCoord>& wp)
{
  Json::Value jsonWp;
  std::string uncompressed;
  if (!xaya::UncompressJson (encoded, MAX_WAYPOINT_SIZE, 3,
                             jsonWp, uncompressed))
    {
      LOG (WARNING)
          << "Failed to decode waypoint string:\n"
          << encoded.substr (0, 1'024);
      return false;
    }

  if (!jsonWp.isArray ())
    {
      LOG (WARNING) << "Decoded waypoints are not an array:\n" << jsonWp;
      return false;
    }

  wp.clear ();
  for (const auto& entry : jsonWp)
    {
      HexCoord c;
      if (!CoordFromJson (entry, c))
        {
          LOG (WARNING) << "Invalid waypoint: " << entry;
          return false;
        }
      wp.push_back (c);
    }

  return true;
}

/* ************************************************************************** */

void
StopCharacter (Character& c)
{
  VLOG (1) << "Stopping movement for " << c.GetId ();
  c.MutableProto ().clear_movement ();
  c.MutableVolatileMv ().Clear ();
}

namespace
{

/**
 * Computes full movement edge weights, using a "base" function and the
 * dynamic obstacle map.
 */
template <typename Fcn>
  inline PathFinder::DistanceT
  FullMovementEdgeWeight (const Fcn& baseEdges, const DynObstacles& dyn,
                          const Faction f,
                          const HexCoord& from, const HexCoord& to)
{
  /* With dynamic obstacles, we do not handle the situation well if from and
     to are the same location.  In that case, the vehicle itself will be
     seen as obstacle (which it should not).  */
  CHECK_NE (from, to);

  const auto res = baseEdges (from, to);

  if (res == PathFinder::NO_CONNECTION)
    return PathFinder::NO_CONNECTION;

  if (dyn.IsBuilding (to) || dyn.HasVehicle (to, f))
    return PathFinder::NO_CONNECTION;

  return res;
}

/**
 * Returns the actual movement speed to use for a character.  This handles
 * a chosen speed reduction if any, as well as combat effects that slow
 * the character as well.
 */
uint32_t
GetCharacterSpeed (const Character& c)
{
  const auto& pb = c.GetProto ();
  int64_t res = pb.speed ();

  const StatModifier modifier(c.GetEffects ().speed ());
  res = modifier (res);
  if (res < 0)
    return 0;

  if (pb.movement ().has_chosen_speed ())
    res = std::min<int64_t> (res, pb.movement ().chosen_speed ());

  CHECK_GE (res, 0);
  return res;
}

/**
 * Tries to step the given character for one hex into the given direction.
 * Returns true if that has been done successfully, and false if it wasn't
 * possible (e.g. because there's an obstacle there or because the
 * remaining movement points do not suffice).
 */
template <typename Fcn>
  bool
  StepCharacter (Character& c, const HexCoord& dir,
                 const Context& ctx, Fcn edges)
{
  const auto& pos = c.GetPosition ();
  const HexCoord dest(pos + dir);

  CHECK_EQ (HexCoord::DistanceL1 (pos, dest), 1);
  const auto dist = edges (pos, dest);
  VLOG (1)
      << "Current step from " << pos << " to " << dest << ": distance " << dist;

  if (dist == PathFinder::NO_CONNECTION)
    {
      LOG (WARNING)
          << "Character " << c.GetId ()
          << " is stepping into obstacle from "
          << pos << " to " << dest;

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

      if (volMv.blocked_turns ()
            > ctx.RoConfig ()->params ().blocked_step_retries ())
        {
          VLOG (1)
              << "Too many blocked turns, stopping character " << c.GetId ();
          StopCharacter (c);
        }

      return false;
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
      return false;
    }

  VLOG (1) << "Performing this step now...";
  c.MutableVolatileMv ().set_partial_step (volMv.partial_step () - dist);
  c.SetPosition (dest);
  return true;
}

template <typename Fcn>
  void
  CharacterMovement (Character& c, const Context& ctx, Fcn edges)
{
  const auto& pb = c.GetProto ();
  CHECK (pb.has_movement ())
      << "Character " << c.GetId ()
      << " was selected for movement but is not actually moving";

  /* In principle, we do not allow to even set waypoints if the speed is
     zero.  But if a retarder is applied, the base speed might be non-zero
     but the actual speed is zero.  So handle that.  */
  const auto speed = GetCharacterSpeed (c);
  if (speed == 0)
    return;
  CHECK_GT (speed, 0);

  VLOG (1)
      << "Processing movement for character: " << c.GetId ()
      << " (native speed: " << pb.speed () << ", effective: " << speed << ")";

  const unsigned partialStep = c.GetVolatileMv ().partial_step () + speed;
  c.MutableVolatileMv ().set_partial_step (partialStep);
  VLOG (1)
      << "Accumulated movement points for this step: " << partialStep;

  while (true)
    {
      CHECK_GT (pb.movement ().waypoints_size (), 0)
          << "Character " << c.GetId ()
          << " has active movement but no waypoints";
      HexCoord nextWp = CoordFromProto (pb.movement ().waypoints (0));

      /* Check this here rather than after stepping, so that we correctly
         handle (i.e. ignore) duplicate waypoints specified for a character.  */
      while (c.GetPosition () == nextWp)
        {
          VLOG (1)
              << "Character " << c.GetId () << " reached waypoint " << nextWp;
          auto& wp
              = *c.MutableProto ().mutable_movement ()->mutable_waypoints ();
          wp.erase (wp.begin ());

          if (wp.empty ())
            {
              VLOG (1) << "No more waypoints";
              StopCharacter (c);
              return;
            }

          nextWp = CoordFromProto (wp[0]);
        }

      HexCoord dir;
      {
        const auto& pos = c.GetPosition ();
        HexCoord::IntT steps;
        if (!pos.IsPrincipalDirectionTo (nextWp, dir, steps))
          {
            LOG (WARNING)
                << "Character " << c.GetId ()
                << " is at " << pos << " with next waypoint " << nextWp
                << ", which is not in principal direction";
            StopCharacter (c);
            return;
          }
      }

      if (!StepCharacter (c, dir, ctx, edges))
        break;
    }
}

} // anonymous namespace

void
ProcessAllMovement (Database& db, DynObstacles& dyn, const Context& ctx)
{
  CharacterTable tbl(db);
  auto res = tbl.QueryMoving ();
  while (res.Step ())
    {
      const auto c = tbl.GetFromResult (res);
      MoveInDynObstacles dynMover(*c, dyn);

      const Faction f = c->GetFaction ();
      const auto baseEdges = [&ctx, f] (const HexCoord& from,
                                        const HexCoord& to)
        {
          return MovementEdgeWeight (ctx.Map (), f, from, to);
        };
      const auto edges = [&ctx, &dyn, f, &baseEdges] (const HexCoord& from,
                                                      const HexCoord& to)
        {

          return FullMovementEdgeWeight (baseEdges, dyn, f, from, to);
        };

      CharacterMovement (*c, ctx, edges);
    }
}

MoveInDynObstacles::MoveInDynObstacles (const Character& c, DynObstacles& d)
  : character(c), dyn(d)
{
  VLOG (1)
      << "Removing character " << character.GetId ()
      << " at position " << character.GetPosition ()
      << " from the dynamic obstacle map before moving it...";
  dyn.RemoveVehicle (character.GetPosition (), character.GetFaction ());
}

MoveInDynObstacles::~MoveInDynObstacles ()
{
  VLOG (1)
      << "Adding back character " << character.GetId ()
      << " at position " << character.GetPosition ()
      << " to the dynamic obstacle map...";
  dyn.AddVehicle (character.GetPosition (), character.GetFaction ());
}

namespace test
{

PathFinder::DistanceT
MovementEdgeWeight (const EdgeWeightFcn& baseEdges, const DynObstacles& dyn,
                    const Faction f,
                    const HexCoord& from, const HexCoord& to)
{
  return FullMovementEdgeWeight (baseEdges, dyn, f, from, to);
}

void
ProcessCharacterMovement (Character& c, const Context& ctx,
                          const EdgeWeightFcn& edges)
{
  return CharacterMovement (c, ctx, edges);
}

/* ************************************************************************** */

} // namespace test

} // namespace pxd
