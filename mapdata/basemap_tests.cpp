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
namespace
{

class BaseMapTests : public testing::Test
{

protected:

  BaseMap map;

};

TEST_F (BaseMapTests, IsOnMap)
{
  EXPECT_TRUE (map.IsOnMap (HexCoord (0, -4064)));
  EXPECT_TRUE (map.IsOnMap (HexCoord (0, 4064)));
  EXPECT_FALSE (map.IsOnMap (HexCoord (0, -4065)));
  EXPECT_FALSE (map.IsOnMap (HexCoord (0, 4065)));

  EXPECT_TRUE (map.IsOnMap (HexCoord (-4064, 0)));
  EXPECT_TRUE (map.IsOnMap (HexCoord (4064, 0)));
  EXPECT_FALSE (map.IsOnMap (HexCoord (-4065, 0)));
  EXPECT_FALSE (map.IsOnMap (HexCoord (4065, 0)));
}

TEST_F (BaseMapTests, MatchesOriginalObstacleData)
{
  std::ifstream in("obstacledata.dat", std::ios_base::binary); 

  const size_t n = Read<int16_t> (in);
  const size_t m = Read<int16_t> (in);
  LOG (INFO)
      << "Checking IsPassable for " << n << " * " << m
      << " = " << (n * m) << " tiles";

  for (size_t i = 0; i < n * m; ++i)
    {
      const auto x = Read<int16_t> (in);
      const auto y = Read<int16_t> (in);
      const bool passable = Read<int16_t> (in);

      const HexCoord c(x, y);
      EXPECT_TRUE (map.IsOnMap (c));
      EXPECT_EQ (map.IsPassable (c), passable);
    }
}

TEST_F (BaseMapTests, EdgeWeights)
{
  const auto& ew = map.GetEdgeWeights ();

  const HexCoord a(0, 0);
  const HexCoord b(1, 0);
  EXPECT_EQ (ew (a, b), 1000);

  const HexCoord outside(-4065, 0);
  const HexCoord inside(-4064, 0);
  ASSERT_FALSE (map.IsOnMap (outside));
  ASSERT_TRUE (map.IsOnMap (inside));
  EXPECT_EQ (ew (outside, inside), PathFinder::NO_CONNECTION);
}

} // anonymous namespace
} // namespace pxd
