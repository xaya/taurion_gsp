#include "movement.hpp"

#include "params.hpp"
#include "protoutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <google/protobuf/repeated_field.h>

#include <utility>
#include <vector>

namespace pxd
{
namespace
{

/**
 * Returns an edge-weight function that has the given distance between
 * tiles and no obstacles.
 */
PathFinder::EdgeWeightFcn
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
PathFinder::EdgeWeightFcn
EdgesWithObstacle (const PathFinder::DistanceT dist)
{
  return [dist] (const HexCoord& from, const HexCoord& to)
    {
      if (from.GetX () == -1 || to.GetX () == -1)
        return PathFinder::NO_CONNECTION;
      return dist;
    };
}

/**
 * Test fixture for the character movement.  It automatically sets up a test
 * character and has convenient functions for setting up its movement data
 * in the database and retrieving the updated data.
 */
class MovementTests : public DBTestWithSchema
{

private:

  /** Params instance, set to mainnet.  */
  const Params params;

  /** Character table used for interacting with the test database.  */
  CharacterTable tbl;

protected:

  MovementTests ()
    : params(xaya::Chain::MAIN), tbl(db)
  {
    const auto h = tbl.CreateNew ("domob", "foo", Faction::RED);
    CHECK_EQ (h->GetId (), 1);
  }

  /**
   * Returns a handle to the test character (for inspection and update).
   */
  CharacterTable::Handle
  GetTest ()
  {
    return tbl.GetById (1);
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
  StepCharacter (const PathFinder::DistanceT speed,
                 const PathFinder::EdgeWeightFcn& edges,
                 const unsigned n)
  {
    for (unsigned i = 0; i < n; ++i)
      {
        ASSERT_TRUE (IsMoving ());
        ProcessCharacterMovement (*GetTest (), speed, params, edges);
      }
  }

  /**
   * Steps the character multiple times and expects that we reach certain
   * points through that.  We expect it to have stopped after the last milestone
   * is reached.
   */
  void
  ExpectSteps (const PathFinder::DistanceT speed,
               const PathFinder::EdgeWeightFcn& edges,
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
  EXPECT_EQ (GetTest ()->GetPosition (), HexCoord (3, 0));
  const auto mv = GetTest ()->GetProto ().movement ();
  EXPECT_EQ (mv.waypoints_size (), 1);
  EXPECT_GT (mv.steps_size (), 0);

  /* Now let the obstacle appear.  This should move the character right up to
     it and then stop.  */
  ExpectSteps (1, EdgesWithObstacle (1),
    {
      {3, HexCoord (0, 0)},
    });
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

// Also: Start obstacles when the character is right inside.

} // anonymous namespace
} // namespace pxd
