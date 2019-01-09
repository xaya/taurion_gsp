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
   * Returns the size of the distances map for the given PathFinder instance.
   * This is exposed through being friends with the class, and can be used
   * in tests to verify that some "quick return" (e.g. for a source that is
   * an obstacle) worked as expected.
   */
  static size_t
  GetDistancesSize (const PathFinder& f)
  {
    return f.distances.size ();
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
        ASSERT_EQ (s.Next (), next.second);
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
  if (IsObstacle (from) || IsObstacle (to))
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
  PathFinder finder(&EdgeWeight, HexCoord (-1, 2));
  ASSERT_EQ (finder.Compute (HexCoord (0, 0), 10), 8);
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
  PathFinder finder(&EdgeWeight, HexCoord (-1, 2));
  ASSERT_EQ (finder.Compute (HexCoord (-1, 2), 0), 0);
  AssertPath (finder.StepPath (HexCoord (-1, 2)), HexCoord (-1, 2), {});
}

TEST_F (PathFinderTests, ThroughX)
{
  PathFinder finder(&EdgeWeight, HexCoord (-1, 2));

  /* We limit the L1 range such that the path "around" the obstacle is
     not possible and thus the path through x has to be taken.  */
  ASSERT_EQ (finder.Compute (HexCoord (0, 0), 3), 14);

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
  PathFinder finder(&EdgeWeight, HexCoord (-10, 0));
  ASSERT_EQ (finder.Compute (HexCoord (-10, 2), 5), PathFinder::NO_CONNECTION);

  /* There should have been some non-trivial trials before giving up.  */
  EXPECT_GT (GetDistancesSize (finder), 20);
}

TEST_F (PathFinderTests, TriesFullRange)
{
  PathFinder finder(&EdgeWeight, HexCoord (100, 100));
  ASSERT_EQ (finder.Compute (HexCoord (200, 200), 2),
             PathFinder::NO_CONNECTION);

  /* The distances map should contain exactly all those coordinates within
     the L1 range of two.  */
  EXPECT_EQ (GetDistancesSize (finder), 19);
}

TEST_F (PathFinderTests, ToObstacle)
{
  PathFinder finder(&EdgeWeight, HexCoord (-10, 1));
  ASSERT_EQ (finder.Compute (HexCoord (0, 0), 1000), PathFinder::NO_CONNECTION);

  /* The search should have died out quickly, namely after visiting just the
     target (even with a large L1 range as above).  */
  EXPECT_EQ (GetDistancesSize (finder), 1);
}

TEST_F (PathFinderTests, FromObstacle)
{
  PathFinder finder(&EdgeWeight, HexCoord (0, 0));
  ASSERT_EQ (finder.Compute (HexCoord (-10, 1), 1000),
             PathFinder::NO_CONNECTION);

  /* The path from an obstacle should have been determined to be unavailable
     right from the start, without computing a lot of stuff.  */
  EXPECT_EQ (GetDistancesSize (finder), 0);
}

TEST_F (PathFinderTests, MultipleSteppers)
{
  PathFinder finder(&EdgeWeight, HexCoord (2, 0));
  ASSERT_EQ (finder.Compute (HexCoord (0, 0), 10), 2);

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

TEST_F (PathFinderTests, RepathingWithEachStep)
{
  /* This test verifies that we get the same "tail" part of a path if we
     do a fresh path-finding from an intermediate position.  That ensures
     that we will get consistent results while a character moves towards
     the next waypoint, independently of when exactly we do recalculations
     of the remaining path (from current position to that waypoint).  */

  const HexCoord source(-20, 0);
  const HexCoord target(-20, 2);

  std::vector<std::pair<HexCoord, PathFinder::DistanceT>> fullPath;
  {
    PathFinder finder(&EdgeWeight, target);
    PathFinder::DistanceT dist = finder.Compute (source, 100);
    ASSERT_NE (dist, PathFinder::NO_CONNECTION);

    auto s = finder.StepPath (source);
    fullPath.emplace_back (s.GetPosition (), dist);
    while (s.HasMore ())
      {
        dist -= s.Next ();
        fullPath.emplace_back (s.GetPosition (), dist);
      }
    ASSERT_EQ (dist, 0);
    ASSERT_EQ (s.GetPosition (), target);
  }

  for (auto i = fullPath.cbegin (); i != fullPath.cend (); ++i)
    {
      PathFinder finder(&EdgeWeight, target);
      ASSERT_EQ (finder.Compute (i->first, 100), i->second);

      auto s = finder.StepPath (i->first);
      for (auto j = i; ; )
        {
          EXPECT_EQ (s.GetPosition (), j->first);

          ++j;
          if (j == fullPath.cend ())
            break;

          ASSERT_TRUE (s.HasMore ());
          s.Next ();
        }
    }
}

} // anonymous namespace
} // namespace pxd
