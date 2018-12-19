#include "coord.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace pxd
{
namespace
{

using CoordTests = testing::Test;

TEST_F (CoordTests, Equality)
{
  const HexCoord a(2, -5);
  const HexCoord aa(2, -5);
  const HexCoord b(-1, -2);

  EXPECT_EQ (a, a);
  EXPECT_EQ (a, aa);
  EXPECT_NE (a, b);
}

TEST_F (CoordTests, LessThan)
{
  const HexCoord a(0, 0);
  const HexCoord b(0, 1);
  const HexCoord c(1, 0);

  EXPECT_LT (a, b);
  EXPECT_LT (a, c);
  EXPECT_LT (b, c);

  EXPECT_FALSE (a < a);
  EXPECT_FALSE (c < b);
}

TEST_F (CoordTests, DistanceL1)
{
  const HexCoord a(-2, 1);
  const HexCoord b(3, -2);

  EXPECT_EQ (HexCoord::DistanceL1 (a, b), 5);
  EXPECT_EQ (HexCoord::DistanceL1 (b, a), 5);

  EXPECT_EQ (HexCoord::DistanceL1 (a, a), 0);
  EXPECT_EQ (HexCoord::DistanceL1 (b, b), 0);
}

TEST_F (CoordTests, GetRing)
{
  EXPECT_EQ (HexCoord (0, 0).GetRing (), 0);
  EXPECT_EQ (HexCoord (-3, 1).GetRing (), 3);
  EXPECT_EQ (HexCoord (0, 2).GetRing (), 2);
}

TEST_F (CoordTests, Hashing)
{
  const HexCoord a(-5, 2);
  const HexCoord aa(-5, 2);
  const HexCoord b(5, -2);
  const HexCoord c(5, 2);

  std::hash<HexCoord> hasher;

  EXPECT_NE (hasher (a), hasher (b));
  EXPECT_NE (hasher (a), hasher (c));
  EXPECT_NE (hasher (b), hasher (c));

  EXPECT_EQ (hasher (a), hasher (aa));
}

TEST_F (CoordTests, UnorderedSet)
{
  const HexCoord a(-5, 2);
  const HexCoord aa(-5, 2);
  const HexCoord b(5, -2);
  const HexCoord c(5, 2);

  std::unordered_set<HexCoord> coords;

  coords.insert (a);
  coords.insert (b);
  EXPECT_EQ (coords.size (), 2);
  EXPECT_EQ (coords.count (aa), 1);
  EXPECT_EQ (coords.count (b), 1);
  EXPECT_EQ (coords.count (c), 0);

  coords.insert (aa);
  EXPECT_EQ (coords.size (), 2);
  EXPECT_EQ (coords.count (a), 1);
  EXPECT_EQ (coords.count (b), 1);
  EXPECT_EQ (coords.count (c), 0);

  coords.insert (c);
  EXPECT_EQ (coords.size (), 3);
  EXPECT_EQ (coords.count (a), 1);
  EXPECT_EQ (coords.count (b), 1);
  EXPECT_EQ (coords.count (c), 1);
}

TEST_F (CoordTests, Neighbours)
{
  const HexCoord centre(-2, 1);

  std::set<HexCoord> neighbours;
  for (const auto& n : centre.Neighbours ())
    {
      EXPECT_EQ (neighbours.count (n), 0);
      neighbours.insert (n);
    }

  EXPECT_EQ (neighbours.size (), 6);
  for (const auto& n : {HexCoord (-3, 1), HexCoord (-2, 0), HexCoord (-1, 0),
                        HexCoord (-1, 1), HexCoord (-2, 2), HexCoord (-3, 2)})
    EXPECT_EQ (neighbours.count (n), 1);

  for (const auto& n : neighbours)
    EXPECT_EQ (HexCoord::DistanceL1 (centre, n), 1);
}

TEST_F (CoordTests, StreamOutput)
{
  std::ostringstream out;
  out << HexCoord (-5, 42);
  EXPECT_EQ (out.str (), "(-5, 42)");
}

} // anonymous namespace
} // namespace pxd
