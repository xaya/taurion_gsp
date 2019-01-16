#include "basemap.hpp"

#include "dataio.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <fstream>

namespace pxd
{

class BaseMapTests : public testing::Test
{

protected:

  BaseMap m;

  inline bool
  IsOnMap (const HexCoord& c) const
  {
    return m.IsOnMap (c);
  }

  inline bool
  IsPassable (const HexCoord& c) const
  {
    return m.IsPassable (c);
  }

};

namespace
{

TEST_F (BaseMapTests, IsOnMap)
{
  EXPECT_TRUE (IsOnMap (HexCoord (0, -4064)));
  EXPECT_TRUE (IsOnMap (HexCoord (0, 4064)));
  EXPECT_FALSE (IsOnMap (HexCoord (0, -4065)));
  EXPECT_FALSE (IsOnMap (HexCoord (0, 4065)));

  EXPECT_TRUE (IsOnMap (HexCoord (-4064, 0)));
  EXPECT_TRUE (IsOnMap (HexCoord (4064, 0)));
  EXPECT_FALSE (IsOnMap (HexCoord (-4065, 0)));
  EXPECT_FALSE (IsOnMap (HexCoord (4065, 0)));
}

TEST_F (BaseMapTests, MatchesOriginalObstacleData)
{
  std::ifstream in("obstacledata.dat", std::ios_base::binary); 

  const int n = ReadInt16 (in);
  const int m = ReadInt16 (in);
  LOG (INFO)
      << "Checking IsPassable for " << n << " * " << m
      << " = " << (n * m) << " tiles";

  for (int i = 0; i < n * m; ++i)
    {
      const int x = ReadInt16 (in);
      const int y = ReadInt16 (in);
      const bool passable = ReadInt16 (in);

      const HexCoord c(x, y);
      EXPECT_TRUE (IsOnMap (c));
      EXPECT_EQ (IsPassable (c), passable);
    }
}

TEST_F (BaseMapTests, EdgeWeights)
{
  const auto& ew = m.GetEdgeWeights ();

  const HexCoord a(0, 0);
  const HexCoord b(1, 0);
  EXPECT_EQ (ew (a, b), 1000);

  const HexCoord outside(-4065, 0);
  const HexCoord inside(-4064, 0);
  ASSERT_FALSE (IsOnMap (outside));
  ASSERT_TRUE (IsOnMap (inside));
  EXPECT_EQ (ew (outside, inside), PathFinder::NO_CONNECTION);
}

} // anonymous namespace
} // namespace pxd
