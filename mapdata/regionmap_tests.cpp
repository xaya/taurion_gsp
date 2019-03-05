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

  /**
   * Verifies that the given region map matches the data from the original
   * data file.  If limit is positive, check only the first few entries.
   */
  void
  VerifyMatchesData (const RegionMap& rm, const size_t limit = -1) const
  {
    std::ifstream in("regiondata.dat", std::ios_base::binary); 

    const size_t n = Read<int16_t> (in);
    const size_t m = Read<int16_t> (in);

    size_t num = n * m;
    LOG (INFO)
        << "Checking region map for " << n << " * " << m
        << " = " << num << " tiles";

    if (limit >= 0 && num > limit)
      {
        num = limit;
        LOG (WARNING)
            << "Checking only first " << num
            << " entries instead of all " << (n * m);
      }

    for (size_t i = 0; i < num; ++i)
      {
        const auto x = Read<int16_t> (in);
        const auto y = Read<int16_t> (in);
        const HexCoord c(x, y);

        const auto id = Read<int32_t> (in);
        ASSERT_EQ (rm.GetRegionForTile (c), id) << "Mismatch for tile " << c;
      }
  }

};

TEST_F (RegionMapTests, InMemory)
{
  auto rm = NewInMemoryRegionMap ("regionmap.bin");
  VerifyMatchesData (*rm);
}

TEST_F (RegionMapTests, Stream)
{
  auto rm = NewStreamRegionMap ("regionmap.bin");
  VerifyMatchesData (*rm, 1000000);
}

TEST_F (RegionMapTests, MemoryMapped)
{
  auto rm = NewMemMappedRegionMap ("regionmap.bin");
  VerifyMatchesData (*rm);
}

TEST_F (RegionMapTests, Compact)
{
  auto rm = NewCompactRegionMap ();
  VerifyMatchesData (*rm);
}

} // anonymous namespace
} // namespace pxd
