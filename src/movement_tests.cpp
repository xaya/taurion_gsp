/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#include "testutils.hpp"
#include "protoutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "hexagonal/coord.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <google/protobuf/repeated_field.h>

#include <utility>
#include <vector>

namespace pxd
{
namespace test
{
namespace
{

/* ************************************************************************** */

class StopCharacterTests : public DBTestWithSchema
{

protected:

  CharacterTable tbl;

  StopCharacterTests ()
    : tbl(db)
  {}

};

TEST_F (StopCharacterTests, Works)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (HexCoord (5, 7));
  c->MutableVolatileMv ().set_partial_step (42);
  auto* mv = c->MutableProto ().mutable_movement ();
  *mv->add_waypoints () = CoordToProto (HexCoord (10, 10));
  c.reset ();

  StopCharacter (*tbl.GetById (id));

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetPosition (), HexCoord (5, 7));
  EXPECT_FALSE (c->GetProto ().has_movement ());
  EXPECT_FALSE (c->GetVolatileMv ().has_partial_step ());
}

TEST_F (StopCharacterTests, AlreadyStopped)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (HexCoord (5, 7));
  c.reset ();

  StopCharacter (*tbl.GetById (id));

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetPosition (), HexCoord (5, 7));
  EXPECT_FALSE (c->GetProto ().has_movement ());
  EXPECT_FALSE (c->GetVolatileMv ().has_partial_step ());
}

/* ************************************************************************** */

/**
 * Returns an edge-weight function that has the given distance between
 * tiles and no obstacles.
 */
EdgeWeightFcn
EdgeWeights (const PathFinder::DistanceT dist)
{
  return [dist] (const HexCoord& from, const HexCoord& to)
    {
      return dist;
    };
}

/**
 * Returns an edge-weight function that has the given distance between
 * neighbouring tiles but also marks all tiles with x=-1 as obstacle.
 */
EdgeWeightFcn
EdgesWithObstacle (const PathFinder::DistanceT dist)
{
  return [dist] (const HexCoord& from, const HexCoord& to)
    {
      if (from.GetX () == -1 || to.GetX () == -1)
        return PathFinder::NO_CONNECTION;
      return dist;
    };
}

/* ************************************************************************** */

class MovementEdgeWeightTests : public DBTestWithSchema
{

protected:

  DynObstacles dyn;

  MovementEdgeWeightTests ()
    : dyn(db)
  {}

};

TEST_F (MovementEdgeWeightTests, BaseEdgesPassedThrough)
{
  const auto baseEdges = EdgesWithObstacle (42);
  EXPECT_EQ (MovementEdgeWeight (HexCoord (0, 0), HexCoord (1, 0),
                                 baseEdges, dyn, Faction::RED),
             42);
  EXPECT_EQ (MovementEdgeWeight (HexCoord (0, 0), HexCoord (-1, 0),
                                 baseEdges, dyn, Faction::RED),
             PathFinder::NO_CONNECTION);
}

TEST_F (MovementEdgeWeightTests, DynamicObstacle)
{
  const auto baseEdges = EdgeWeights (42);
  dyn.AddVehicle (HexCoord (0, 0), Faction::RED);

  EXPECT_EQ (MovementEdgeWeight (HexCoord (1, 0), HexCoord (0, 0),
                                 baseEdges, dyn, Faction::RED),
             PathFinder::NO_CONNECTION);
  EXPECT_EQ (MovementEdgeWeight (HexCoord (1, 0), HexCoord (0, 0),
                                 baseEdges, dyn, Faction::GREEN),
             42);
}

/* ************************************************************************** */

/**
 * Test fixture for the character movement.  It automatically sets up a test
 * character and has convenient functions for setting up its movement data
 * in the database and retrieving the updated data.
 */
class MovementTests : public DBTestWithSchema
{

protected:

  ContextForTesting ctx;

  CharacterTable tbl;

  MovementTests ()
    : tbl(db)
  {
    const auto h = tbl.CreateNew ("domob", Faction::RED);
    CHECK_EQ (h->GetId (), 1);
  }

  /**
   * Returns a handle to the test character (for inspection and update).
   */
  CharacterTable::Handle
  GetTest ()
  {
    auto h = tbl.GetById (1);
    CHECK (h != nullptr);
    return h;
  }

  /**
   * Returns whether or not the test character is still moving.
   */
  bool
  IsMoving ()
  {
    return GetTest ()->GetProto ().has_movement ();
  }

  /**
   * Sets the test character's waypoints from the given vector.
   */
  void
  SetWaypoints (const std::vector<HexCoord>& coords)
  {
    const auto h = GetTest ();
    auto* mv = h->MutableProto ().mutable_movement ();
    SetRepeatedCoords (coords, *mv->mutable_waypoints ());
  }

  /**
   * Processes n movement steps for the test character.
   */
  void
  StepCharacter (const PathFinder::DistanceT speed, const EdgeWeightFcn& edges,
                 const unsigned n)
  {
    GetTest ()->MutableProto ().set_speed (speed);
    for (unsigned i = 0; i < n; ++i)
      {
        ASSERT_TRUE (IsMoving ());
        ProcessCharacterMovement (*GetTest (), ctx, edges);
      }
  }

  /**
   * Steps the character multiple times and expects that we reach certain
   * points through that.  We expect it to have stopped after the last milestone
   * is reached.
   */
  void
  ExpectSteps (const PathFinder::DistanceT speed, const EdgeWeightFcn& edges,
               const std::vector<std::pair<unsigned, HexCoord>>& milestones)
  {
    for (const auto& m : milestones)
      {
        EXPECT_TRUE (IsMoving ());
        StepCharacter (speed, edges, m.first);
        EXPECT_EQ (GetTest ()->GetPosition (), m.second);
      }
    EXPECT_FALSE (IsMoving ());
  }

};

TEST_F (MovementTests, Basic)
{
  SetWaypoints ({HexCoord (10, 2), HexCoord (10, 5)});
  ExpectSteps (1, EdgeWeights (1),
    {
      {12, HexCoord (10, 2)},
      {3, HexCoord (10, 5)},
    });
}

TEST_F (MovementTests, SlowSpeed)
{
  SetWaypoints ({HexCoord (3, 0)});
  ExpectSteps (2, EdgeWeights (3),
    {
      {4, HexCoord (2, 0)},
      {1, HexCoord (3, 0)},
    });
}

TEST_F (MovementTests, FastSpeed)
{
  SetWaypoints ({HexCoord (3, 0), HexCoord (-3, 0)});
  ExpectSteps (7, EdgeWeights (1),
    {
      {1, HexCoord (-1, 0)},
      {1, HexCoord (-3, 0)},
    });
}

TEST_F (MovementTests, SlowChosenSpeed)
{
  SetWaypoints ({HexCoord (10, 0)});
  GetTest ()->MutableProto ().mutable_movement ()->set_chosen_speed (1);
  ExpectSteps (5, EdgeWeights (1),
    {
      {5, HexCoord (5, 0)},
      {5, HexCoord (10, 0)},
    });
}

TEST_F (MovementTests, FastChosenSpeed)
{
  SetWaypoints ({HexCoord (10, 0)});
  GetTest ()->MutableProto ().mutable_movement ()->set_chosen_speed (5);
  ExpectSteps (1, EdgeWeights (1),
    {
      {5, HexCoord (5, 0)},
      {5, HexCoord (10, 0)},
    });
}

TEST_F (MovementTests, DuplicateWaypoints)
{
  SetWaypoints (
    {
      HexCoord (0, 0),
      HexCoord (1, 0), HexCoord (1, 0),
      HexCoord (2, 0), HexCoord (2, 0),
    });
  ExpectSteps (1, EdgeWeights (1),
    {
      {1, HexCoord (1, 0)},
      {1, HexCoord (2, 0)},
    });
}

TEST_F (MovementTests, WaypointsTooFar)
{
  SetWaypoints ({HexCoord (100, 0), HexCoord (201, 0)});
  ExpectSteps (1, EdgeWeights (10),
    {
      {1000, HexCoord (100, 0)},
    });
}

TEST_F (MovementTests, ObstacleInWaypoints)
{
  SetWaypoints ({HexCoord (0, 5), HexCoord (-2, 5)});
  ExpectSteps (1, EdgesWithObstacle (1),
    {
      {5, HexCoord (0, 5)},
    });
}

TEST_F (MovementTests, ObstacleInSteps)
{
  SetWaypoints ({HexCoord (5, 0), HexCoord (-10, 0)});

  /* Step first without the obstacle, so that the final steps are already
     planned through where it will be later on.  */
  StepCharacter (1, EdgeWeights (1), 7);
  EXPECT_TRUE (IsMoving ());
  auto h = GetTest ();
  EXPECT_EQ (h->GetPosition (), HexCoord (3, 0));
  const auto& mv = h->GetProto ().movement ();
  EXPECT_EQ (mv.waypoints_size (), 1);
  EXPECT_GT (mv.steps_size (), 0);

  /* Now let the obstacle appear.  This should move the character right up to
     it, retry there for a couple blocks, and then stop.  */
  ExpectSteps (1, EdgesWithObstacle (1),
    {
      /* After three blocks we reach the obstacle.  */
      {3, HexCoord (0, 0)},

      /* After ten more blocks we stop retrying.  */
      {9, HexCoord (0, 0)},
      {1, HexCoord (0, 0)},
    });
}

TEST_F (MovementTests, BlockedTurns)
{
  SetWaypoints ({HexCoord (5, 0), HexCoord (-10, 0)});

  /* Step first without the obstacle, so that the final steps are already
     planned through where it will be later on.  */
  StepCharacter (1, EdgeWeights (1), 10);
  EXPECT_TRUE (IsMoving ());
  auto h = GetTest ();
  EXPECT_EQ (h->GetPosition (), HexCoord (0, 0));
  const auto& mv = h->GetProto ().movement ();
  EXPECT_EQ (mv.waypoints_size (), 1);
  EXPECT_GT (mv.steps_size (), 0);
  EXPECT_FALSE (h->GetVolatileMv ().has_blocked_turns ());

  /* Try stepping into the obstacle, which should increment the blocked turns
     counter and reset any partial step progress.  */
  GetTest ()->MutableVolatileMv ().set_partial_step (500);
  StepCharacter (1, EdgesWithObstacle (1000), 10);
  EXPECT_EQ (GetTest ()->GetPosition (), HexCoord (0, 0));
  EXPECT_TRUE (IsMoving ());
  EXPECT_FALSE (GetTest ()->GetVolatileMv ().has_partial_step ());
  EXPECT_EQ (GetTest ()->GetVolatileMv ().blocked_turns (), 10);

  /* Stepping with free way (even if we can't do a full step) will reset
     the counter again.  */
  StepCharacter (1, EdgeWeights (1000), 1);
  EXPECT_EQ (GetTest ()->GetPosition (), HexCoord (0, 0));
  EXPECT_TRUE (IsMoving ());
  EXPECT_EQ (GetTest ()->GetVolatileMv ().partial_step (), 1);
  EXPECT_FALSE (GetTest ()->GetVolatileMv ().has_blocked_turns ());

  /* Trying too often will stop movement.  */
  StepCharacter (1, EdgesWithObstacle (1000), 11);
  EXPECT_EQ (GetTest ()->GetPosition (), HexCoord (0, 0));
  EXPECT_FALSE (IsMoving ());
  EXPECT_FALSE (GetTest ()->GetVolatileMv ().has_partial_step ());
  EXPECT_FALSE (GetTest ()->GetVolatileMv ().has_blocked_turns ());
}

TEST_F (MovementTests, CharacterInObstacle)
{
  /* This is a situation that should not actually appear in practice.  But it
     is good to ensure it behaves as expected anyway.  */
  GetTest ()->SetPosition (HexCoord (-1, 0));
  SetWaypoints ({HexCoord (10, 0)});
  ExpectSteps (1, EdgesWithObstacle (1),
    {
      {1, HexCoord (-1, 0)},
    });
}

/* ************************************************************************** */

class AllMovementTests : public MovementTests
{

protected:

  /**
   * Steps all characters for one block.  This constructs a fresh dynamic
   * obstacle map from the database (as is done in the real game logic).
   */
  void
  StepAll ()
  {
    DynObstacles dyn(db);
    ProcessAllMovement (db, dyn, ctx);
  }

};

TEST_F (AllMovementTests, LongSteps)
{
  /* This test verifies that we are able to perform many steps in a single
     block.  In particular, this only works if updating the dynamic obstacle
     map for the vehicle being moved works correctly.  */

  auto c = GetTest ();
  c->MutableProto ().set_speed (1);
  c->MutableVolatileMv ().set_partial_step (1000000);
  c.reset ();

  SetWaypoints ({
    HexCoord (5, 0),
    HexCoord (5, 0),
    HexCoord (0, 0),
    HexCoord (0, 0),
    HexCoord (2, 0),
    HexCoord (10, 0),
    HexCoord (-10, 0),
    HexCoord (-10, 0),
  });
  StepAll ();

  EXPECT_FALSE (IsMoving ());
  EXPECT_EQ (GetTest ()->GetPosition (), HexCoord (-10, 0));
}

TEST_F (AllMovementTests, OtherVehicles)
{
  /* Movement is processed ordered by the character ID.  Thus when multiple
     vehicles move onto the same tile through their steps, then the one with
     lowest ID takes precedence.  */

  /* Move the test character from the fixture out of the way.  */
  GetTest ()->SetPosition (HexCoord (100, 0));

  /* Helper function to create one of our characters set up to move to
     the origin in the next step.  */
  const auto setupChar = [this] (const Faction f, const HexCoord& pos)
    {
      auto c = tbl.CreateNew ("domob", f);

      c->MutableProto ().set_speed (1000);
      c->SetPosition (pos);

      auto* mv = c->MutableProto ().mutable_movement ();
      *mv->add_waypoints () = CoordToProto (HexCoord (0, 0));
      *mv->add_steps () = CoordToProto (c->GetPosition ());

      return c->GetId ();
    };

  const auto id1 = setupChar (Faction::RED, HexCoord (1, 0));
  const auto id2 = setupChar (Faction::RED, HexCoord (-1, 0));
  ASSERT_GT (id2, id1);
  const auto id3 = setupChar (Faction::GREEN, HexCoord (0, 1));
  ASSERT_GT (id3, id2);

  StepAll ();

  EXPECT_EQ (tbl.GetById (id1)->GetPosition (), HexCoord (0, 0));
  EXPECT_EQ (tbl.GetById (id2)->GetPosition (), HexCoord (-1, 0));
  EXPECT_EQ (tbl.GetById (id3)->GetPosition (), HexCoord (0, 0));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace test
} // namespace pxd
