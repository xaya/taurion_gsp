#include "basemap.hpp"

#include "dataio.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <cstdint>
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

  const int n = Read<int16_t> (in);
  const int m = Read<int16_t> (in);
  LOG (INFO)
      << "Checking IsPassable for " << n << " * " << m
      << " = " << (n * m) << " tiles";

  for (int i = 0; i < n * m; ++i)
    {
      const int x = Read<int16_t> (in);
      const int y = Read<int16_t> (in);
      const bool passable = Read<int16_t> (in);

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
