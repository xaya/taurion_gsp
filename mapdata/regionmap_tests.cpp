#include "regionmap.hpp"

#include "dataio.hpp"

#include "hexagonal/coord.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <cstdint>
#include <fstream>

namespace pxd
{
namespace
{

class RegionMapTests : public testing::Test
{

protected:

  RegionMap rm;

};

TEST_F (RegionMapTests, OutOfMap)
{
  EXPECT_NE (rm.GetRegionId (HexCoord (0, 4064)), RegionMap::OUT_OF_MAP);
  EXPECT_EQ (rm.GetRegionId (HexCoord (0, 4065)), RegionMap::OUT_OF_MAP);
}

TEST_F (RegionMapTests, MatchesOriginalData)
{
  std::ifstream in("regiondata.dat", std::ios_base::binary); 

  const size_t n = Read<int16_t> (in);
  const size_t m = Read<int16_t> (in);

  const size_t num = n * m;
  LOG (INFO)
      << "Checking region map for " << n << " * " << m
      << " = " << num << " tiles";

  for (size_t i = 0; i < num; ++i)
    {
      const auto x = Read<int16_t> (in);
      const auto y = Read<int16_t> (in);
      const HexCoord c(x, y);

      const auto id = Read<int32_t> (in);
      ASSERT_EQ (rm.GetRegionId (c), id) << "Mismatch for tile " << c;
    }
}

TEST_F (RegionMapTests, GetRegionShape)
{
  const HexCoord coords[] =
    {
      HexCoord (0, -4064),
      HexCoord (0, 4064),
      HexCoord (-4064, 0),
      HexCoord (4064, 0),
      HexCoord (0, 0),
    };

  for (const auto& c : coords)
    {
      RegionMap::IdT id;
      const std::set<HexCoord> tiles = rm.GetRegionShape (c, id);

      EXPECT_EQ (id, rm.GetRegionId (c));

      for (const auto& t : tiles)
        {
          EXPECT_EQ (id, rm.GetRegionId (t));
          for (const auto& n : t.Neighbours ())
            {
              if (tiles.count (n) > 0)
                continue;
              EXPECT_NE (id, rm.GetRegionId (n));
            }
        }
    }
}

} // anonymous namespace
} // namespace pxd
