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

#include "coord.hpp"
#include "pathfinder.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace pxd
{

/**
 * Basic test fixture for tests of the PathFinder class.  It defines an
 * edge-weight function with a test setup of obstacles / speeds.
 */
class PathFinderTests : public testing::Test
{

protected:

  /**
   * Returns the number of computed tiles from the last path finding.
   * This is exposed through being friends with the class, and can be used
   * in tests to verify that some "quick return" (e.g. for a source that is
   * an obstacle) worked as expected.
   */
  static size_t
  GetComputedTiles (const PathFinder& f)
  {
    return f.computedTiles;
  }

  /**
   * Edge-weight function for the test setup.  It defines a setting as
   * in the sketch below.
   */
  static PathFinder::DistanceT EdgeWeight (const HexCoord& from,
                                           const HexCoord& to);

  /**
   * Fully steps through the found path and verifies that it returns
   * exactly the given coordinates and distances.
   */
  static void
  AssertPath (PathFinder::Stepper s, const HexCoord& start,
              const std::vector<std::pair<HexCoord, PathFinder::DistanceT>>&
                  golden)
  {
    ASSERT_EQ (s.GetPosition (), start);
    for (const  auto& next : golden)
      {
        ASSERT_TRUE (s.HasMore ());
        ASSERT_EQ (s.Next (), next.second)
            << "expected position: " << next.first
            << ", actual position: " << s.GetPosition ();
        ASSERT_EQ (s.GetPosition (), next.first);
      }
    ASSERT_FALSE (s.HasMore ());
  }

};

/* Test situation defined by EdgeWeight:
 *
 *    . . . . . . .
 *   # # # # # x # .
 *    . . . o . . .
 *
 * Here, # are obstacles, . are tiles with distance one and x is a tile
 * with "distance" 12 to cross (10 more than if it were .).  o is the origin
 * of the coordinate system.  */

namespace
{

bool
IsX (const HexCoord& c)
{
  return c == HexCoord (1, 1);
}

bool
IsObstacle (const HexCoord& c)
{
  return !IsX (c) && c.GetY () == 1 && c.GetX () <= 2;
}

} // anonymous namespace

PathFinder::DistanceT
PathFinderTests::EdgeWeight (const HexCoord& from, const HexCoord& to)
{
  if (IsObstacle (to))
    return PathFinder::NO_CONNECTION;

  if (IsX (from) || IsX (to))
    return 6;

  return 1;
}

/* ************************************************************************** */

namespace
{

TEST_F (PathFinderTests, BasicPath)
{
  PathFinder finder(HexCoord (-1, 2));
  ASSERT_EQ (finder.Compute (&EdgeWeight, HexCoord (0, 0), 10), 8);
  AssertPath (finder.StepPath (HexCoord (0, 0)),
              HexCoord (0, 0), {
                  {HexCoord (1, 0), 1},
                  {HexCoord (2, 0), 1},
                  {HexCoord (3, 0), 1},
                  {HexCoord (3, 1), 1},
                  {HexCoord (2, 2), 1},
                  {HexCoord (1, 2), 1},
                  {HexCoord (0, 2), 1},
                  {HexCoord (-1, 2), 1},
              });
}

TEST_F (PathFinderTests, SourceIsTarget)
{
  PathFinder finder(HexCoord (-1, 2));
  ASSERT_EQ (finder.Compute (&EdgeWeight, HexCoord (-1, 2), 0), 0);
  AssertPath (finder.StepPath (HexCoord (-1, 2)), HexCoord (-1, 2), {});
}

TEST_F (PathFinderTests, FullRange)
{
  PathFinder finder(HexCoord (-3, 0));
  ASSERT_EQ (finder.Compute (&EdgeWeight, HexCoord (0, 0), 3), 3);
  AssertPath (finder.StepPath (HexCoord (0, 0)),
              HexCoord (0, 0), {
                {HexCoord (-1, 0), 1},
                {HexCoord (-2, 0), 1},
                {HexCoord (-3, 0), 1},
              });
}

TEST_F (PathFinderTests, ThroughX)
{
  PathFinder finder(HexCoord (-1, 2));

  /* We limit the L1 range such that the path "around" the obstacle is
     not possible and thus the path through x has to be taken.  */
  ASSERT_EQ (finder.Compute (&EdgeWeight, HexCoord (0, 0), 3), 14);

  AssertPath (finder.StepPath (HexCoord (0, 0)),
              HexCoord (0, 0), {
                  {HexCoord (1, 0), 1},
                  {HexCoord (1, 1), 6},
                  {HexCoord (0, 2), 6},
                  {HexCoord (-1, 2), 1},
              });
}

TEST_F (PathFinderTests, NoPathWithinRange)
{
  PathFinder finder(HexCoord (-10, 0));
  ASSERT_EQ (finder.Compute (&EdgeWeight, HexCoord (-10, 2), 5),
             PathFinder::NO_CONNECTION);

  /* There should have been some non-trivial trials before giving up.  */
  EXPECT_GT (GetComputedTiles (finder), 20);
}

TEST_F (PathFinderTests, OutOfL1Range)
{
  PathFinder finder(HexCoord (100, 100));
  ASSERT_EQ (finder.Compute (&EdgeWeight, HexCoord (200, 200), 2),
             PathFinder::NO_CONNECTION);

  /* We should have returned quickly, without computing any distances.  */
  EXPECT_EQ (GetComputedTiles (finder), 0);

}

TEST_F (PathFinderTests, ToObstacle)
{
  PathFinder finder(HexCoord (-10, 1));
  ASSERT_EQ (finder.Compute (&EdgeWeight, HexCoord (0, 0), 1000),
             PathFinder::NO_CONNECTION);

  /* The search should have died out quickly, namely after visiting just the
     target (even with a large L1 range as above).  */
  EXPECT_EQ (GetComputedTiles (finder), 1);
}

TEST_F (PathFinderTests, FromObstacle)
{
  /* This is a custom obstacle map, which has obstacles all in the upper
     half (y > 0).  That way, we actually have coordinates that are surrounded
     by obstacles.  For them, the search should return immediately and
     determine that they are not accessible.  */

  const auto edges = [] (const HexCoord& from, const HexCoord& to)
                        -> PathFinder::DistanceT
    {
      if (to.GetY () > 0)
        return PathFinder::NO_CONNECTION;

      return 1;
    };

  PathFinder finder(HexCoord (0, -1));
  ASSERT_EQ (finder.Compute (edges, HexCoord (0, 2), 1000),
             PathFinder::NO_CONNECTION);

  /* The path from an obstacle should have been determined to be unavailable
     right from the start, without computing a lot of stuff.  */
  EXPECT_EQ (GetComputedTiles (finder), 0);
}

TEST_F (PathFinderTests, MultipleSteppers)
{
  PathFinder finder(HexCoord (2, 0));
  ASSERT_EQ (finder.Compute (&EdgeWeight, HexCoord (0, 0), 10), 2);

  auto s1 = finder.StepPath (HexCoord (0, 0));
  ASSERT_TRUE (s1.HasMore ());
  ASSERT_EQ (s1.GetPosition (), HexCoord (0, 0));
  ASSERT_EQ (s1.Next (), 1);

  auto s2 = finder.StepPath (HexCoord (0, 0));
  AssertPath (std::move (s2),
              HexCoord (0, 0), {
                  {HexCoord (1, 0), 1},
                  {HexCoord (2, 0), 1},
              });

  AssertPath (std::move (s1),
              HexCoord (1, 0), {
                  {HexCoord (2, 0), 1},
              });
}

TEST_F (PathFinderTests, StepperAvoidsTurns)
{
  /* Stepping a path tries the directions according to a fixed order
     (when multiple paths have the same ultimate length), but it should
     also avoid turns as much as possible and keep directions once chosen
     and not zig-zag around.  To test this, we use a map that looks like this:

      . . . x .
     . . . # .
      o # . . .

     Here, o is the origin and also the start point, # are obstacles, . are
     free spaces and x is the target of the path.  The direction preference
     in general is "east" before "north east".

     So by strictly following the direction preference at each step, the
     stepper would go NE, E, NE, E.  But instead, to avoid the extra turns,
     it should now go NE, NE, E, E.  */

  const auto edges = [] (const HexCoord& from, const HexCoord& to)
                        -> PathFinder::DistanceT
    {
      if (to == HexCoord (1, 0) || to == HexCoord (2, 1))
        return PathFinder::NO_CONNECTION;

      return 1;
    };

  PathFinder finder(HexCoord (2, 2));
  ASSERT_EQ (finder.Compute (edges, HexCoord (0, 0), 10), 4);

  AssertPath (finder.StepPath (HexCoord (0, 0)),
              HexCoord (0, 0), {
                  {HexCoord (0, 1), 1},
                  {HexCoord (0, 2), 1},
                  {HexCoord (1, 2), 1},
                  {HexCoord (2, 2), 1},
              });
}

} // anonymous namespace
} // namespace pxd
