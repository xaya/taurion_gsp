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
      ASSERT_EQ (rm.GetRegionForTile (c), id) << "Mismatch for tile " << c;
    }
}

} // anonymous namespace
} // namespace pxd
