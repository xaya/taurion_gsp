/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#include "resourcedist.hpp"

#include "protoutils.hpp"
#include "testutils.hpp"

#include "mapdata/basemap.hpp"
#include "mapdata/tiledata.hpp"
#include "proto/roconfig.hpp"

#include <xayautil/hash.hpp>
#include <xayautil/uint256.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <map>
#include <vector>

namespace pxd
{
namespace internal
{
namespace
{

/* ************************************************************************** */

using ResourceFallOffTests = testing::Test;

TEST_F (ResourceFallOffTests, Clipping)
{
  EXPECT_EQ (FallOff (0, 10), 10);
  EXPECT_EQ (FallOff (400, 10), 10);
  EXPECT_EQ (FallOff (1'001, 10), 0);
}

TEST_F (ResourceFallOffTests, ValueOne)
{
  EXPECT_EQ (FallOff (400, 1), 1);
  EXPECT_EQ (FallOff (1'000, 1), 1);
}

TEST_F (ResourceFallOffTests, ValueTwo)
{
  EXPECT_EQ (FallOff (400, 2), 2);
  EXPECT_EQ (FallOff (401, 2), 1);
  EXPECT_EQ (FallOff (1'000, 2), 1);
}

TEST_F (ResourceFallOffTests, ValueThree)
{
  EXPECT_EQ (FallOff (400, 3), 3);
  EXPECT_EQ (FallOff (401, 3), 2);
  EXPECT_EQ (FallOff (700, 3), 2);
  EXPECT_EQ (FallOff (701, 3), 1);
  EXPECT_EQ (FallOff (1'000, 3), 1);
}

TEST_F (ResourceFallOffTests, Monotone)
{
  for (unsigned dist = 0; dist < 1'010; ++dist)
    EXPECT_LE (FallOff (dist + 1, 3), FallOff (dist, 3));
}

TEST_F (ResourceFallOffTests, LargeValue)
{
  constexpr auto val = std::numeric_limits<uint32_t>::max ();
  EXPECT_EQ (FallOff (0, val), val);
  EXPECT_EQ (FallOff (400, val), val);
  EXPECT_EQ (FallOff (700, val), val / 2 + 1);
  EXPECT_EQ (FallOff (1'000, val), 1);
  EXPECT_EQ (FallOff (1'001, val), 0);

  for (unsigned dist = 400; dist <= 1'000; ++dist)
    EXPECT_LT (FallOff (dist + 1, val), FallOff (dist, val));
}

/* ************************************************************************** */

class DetectResourceTests : public testing::Test
{

protected:

  TestRandom rnd;

  /**
   * The resource distribution data that we pass to DetectResource.  It is
   * set to the official config data initially, but tests may modify it as
   * they need for their setup.
   */
  proto::ResourceDistribution rd;

  /** Output variable for the detected type of resource.  */
  std::string type;
  /** Output variable for the detected amount.  */
  Inventory::QuantityT amount;

  DetectResourceTests ()
    : rd(RoConfig (xaya::Chain::REGTEST)->resource_dist ())
  {}

  /**
   * Calls DetectResource with our random instance and config data.
   */
  void
  Detect (const HexCoord& pos)
  {
    DetectResource (pos, rd, rnd, type, amount);
  }

  /**
   * Adds a resource area to our config data.
   */
  void
  AddArea (const HexCoord& centre, const std::vector<std::string>& types)
  {
    auto* area = rd.add_areas ();
    *area->mutable_centre () = CoordToProto (centre);
    for (const auto& t : types)
      area->add_resources (t);
  }

};

TEST_F (DetectResourceTests, NothingAvailable)
{
  rd.clear_areas ();
  AddArea (HexCoord (2000, -3000), {"raw i"});

  Detect (HexCoord (0, 0));
  EXPECT_EQ (type, "raw a");
  EXPECT_EQ (amount, 0);
}

TEST_F (DetectResourceTests, RandomType)
{
  constexpr unsigned trials = 100'000;
  constexpr unsigned eps = trials / 100;

  rd.clear_areas ();
  AddArea (HexCoord (0, 0), {"raw a", "raw b"});
  AddArea (HexCoord (0, 700), {"raw a"});
  AddArea (HexCoord (700, 0), {"raw i"});

  std::map<std::string, unsigned> typesFound;
  for (unsigned i = 0; i < trials; ++i)
    {
      Detect (HexCoord (0, 0));
      ASSERT_GT (amount, 0);
      ++typesFound[type];
    }

  for (const auto& entry : typesFound)
    LOG (INFO) << "Found " << entry.first << ": " << entry.second << " times";

  const std::map<std::string, unsigned> expected =
    {
      {"raw a", trials * 3 / 6},
      {"raw b", trials * 2 / 6},
      {"raw i", trials * 1 / 6},
    };
  for (const auto& entry : typesFound)
    {
      const auto mit = expected.find (entry.first);
      ASSERT_TRUE (mit != expected.end ())
          << "Unexpected type: " << entry.first;

      EXPECT_GE (entry.second, mit->second - eps);
      EXPECT_LE (entry.second, mit->second + eps);
    }
}

TEST_F (DetectResourceTests, TypeOrderDeterministic)
{
  const xaya::uint256 seed = xaya::SHA256::Hash ("seed");

  rd.clear_areas ();
  AddArea (HexCoord (0, 0), {"raw a", "raw b"});
  rnd.Seed (seed);
  Detect (HexCoord (0, 0));
  const std::string firstType = type;
  LOG (INFO) << "First attempt detected " << firstType;

  rd.clear_areas ();
  AddArea (HexCoord (0, 0), {"raw b", "raw a"});
  rnd.Seed (seed);
  Detect (HexCoord (0, 0));
  const std::string secondType = type;
  LOG (INFO) << "Second attempt detected " << secondType;

  EXPECT_EQ (firstType, secondType);
}

TEST_F (DetectResourceTests, RandomAmount)
{
  constexpr unsigned trials = 1'000;
  constexpr unsigned baseAmount = 3;
  constexpr unsigned threshold = (trials * 90) / (100 * (baseAmount + 1));

  rd.clear_areas ();
  AddArea (HexCoord (0, 0), {"raw g"});
  rd.clear_base_amounts ();
  rd.mutable_base_amounts ()->insert ({"raw g", baseAmount});

  std::map<Inventory::QuantityT, unsigned> counts;
  for (unsigned i = 0; i < trials; ++i)
    {
      Detect (HexCoord (0, 0));
      ASSERT_EQ (type, "raw g");
      ++counts[amount];
    }

  for (const auto& entry : counts)
    LOG (INFO)
        << "Found " << entry.first << " units : "
        << entry.second << " times";

  ASSERT_EQ (counts.size (), baseAmount + 1);
  for (Inventory::QuantityT i = baseAmount; i <= 2 * baseAmount; ++i)
    EXPECT_GE (counts[i], threshold);
}

TEST_F (DetectResourceTests, MinimumAmount)
{
  constexpr unsigned trials = 100'000;

  rd.clear_areas ();
  AddArea (HexCoord (0, 0), {"raw g"});
  rd.clear_base_amounts ();
  rd.mutable_base_amounts ()->insert ({"raw g", 2});

  std::map<Inventory::QuantityT, unsigned> counts;
  for (unsigned i = 0; i < trials; ++i)
    {
      Detect (HexCoord (900, 0));
      ASSERT_EQ (type, "raw g");
      ++counts[amount];
    }

  ASSERT_EQ (counts.size (), 2);
  EXPECT_GT (counts[1], 0);
  EXPECT_GT (counts[2], 0);
}

TEST_F (DetectResourceTests, AmountPerType)
{
  constexpr unsigned trials = 100'000;

  rd.clear_areas ();
  AddArea (HexCoord (0, 0), {"raw a"});
  AddArea (HexCoord (700, 0), {"raw i"});
  rd.clear_base_amounts ();
  rd.mutable_base_amounts ()->insert ({"raw a", 1'000});
  rd.mutable_base_amounts ()->insert ({"raw i", 50});

  std::map<std::string, Inventory::QuantityT> amounts;
  for (unsigned i = 0; i < trials; ++i)
    {
      Detect (HexCoord (0, 0));
      amounts[type] += amount;
    }

  for (const auto& entry : amounts)
    LOG (INFO) << "Found total " << entry.second << " of " << entry.first;

  const std::map<std::string, Inventory::QuantityT> expected =
    {
      {"raw a", 1'500 * trials * 2 / 3},
      {"raw i", 75 / 2 * trials * 1 / 3},
    };

  ASSERT_EQ (amounts.size (), 2);
  for (const auto& entry : amounts)
    {
      const auto mit = expected.find (entry.first);
      ASSERT_TRUE (mit != expected.end ())
          << "Unexpected type: " << entry.first;

      EXPECT_GE (entry.second, mit->second * 95 / 100);
      EXPECT_LE (entry.second, mit->second * 105 / 100);
    }
}

TEST_F (DetectResourceTests, DISABLED_AllPassableTiles)
{
  /* This test runs detection on all passable tiles to make sure that
     it works.  It also counts basic stats on how many of those are without
     anything to mine.  It runs for quite some time, so is disabled by default.
     It can be run on-demand as needed.  */

  using namespace tiledata;

  BaseMap map;

  unsigned all = 0;
  unsigned passable = 0;
  unsigned nothing = 0;

  for (HexCoord::IntT y = minY; y <= maxY; ++y)
    {
      const auto yInd = y - minY;
      for (HexCoord::IntT x = minX[yInd]; x <= maxX[yInd]; ++x)
        {
          const HexCoord pos(x, y);
          CHECK (map.IsOnMap (pos));

          ++all;
          if (all % 1'000'000 == 0)
            LOG (INFO) << "Tile " << all << " / " << numTiles << "...";

          if (!map.IsPassable (pos))
            continue;
          ++passable;

          Detect (pos);

          if (amount == 0)
            ++nothing;
        }
    }

  ASSERT_EQ (all, numTiles);
  LOG (INFO) << "Detected resources on all " << passable << " passable tiles";
  LOG (INFO) << "Nothing was found " << nothing << " times";
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace internal
} // namespace pxd
