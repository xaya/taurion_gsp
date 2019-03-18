#include "dyntiles.hpp"

#include "tiledata.hpp"

#include "hexagonal/coord.hpp"

#include <gtest/gtest.h>

#include <functional>

namespace pxd
{
namespace
{

/**
 * Performs the callback for all hex coordinates on the full map.
 */
void
ForEachTile (const std::function<void (const HexCoord& c)>& cb)
{
  using namespace tiledata;
  for (HexCoord::IntT y = minY; y <= maxY; ++y)
    {
      const auto yInd = y - minY;
      for (HexCoord::IntT x = minX[yInd]; x <= maxX[yInd]; ++x)
        cb (HexCoord (x, y));
    }
}

using DynTilesTests = testing::Test;

TEST_F (DynTilesTests, FullMap)
{
  DynTiles<bool> m(true);
  ForEachTile ([&m] (const HexCoord& c)
    {
      ASSERT_TRUE (m.Get (c));
      auto ref = m.Access (c);
      ASSERT_TRUE (ref);
      ref = false;
    });
  ForEachTile ([&m] (const HexCoord& c)
    {
      ASSERT_FALSE (m.Get (c));
      ASSERT_FALSE (m.Access (c));
    });
}

} // anonymous namespace
} // namespace pxd
