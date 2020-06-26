/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "safezones.hpp"

#include "tiledata.hpp"

#include "hexagonal/coord.hpp"
#include "proto/config.pb.h"
#include "proto/roconfig.hpp"

#include <xayagame/gamelogic.hpp>

#include <gtest/gtest.h>
#include <glog/logging.h>

namespace pxd
{
namespace
{

/** Coordinate in a neutral safe zone.  */
const HexCoord NEUTRAL(2'042, 10);
/** Coordinate in red's starter zone.  */
const HexCoord RED_START(-2'042, 100);
/** Coordinate in no safe zone.  */
const HexCoord NORMAL(2'042, 11);

class SafeZonesTests : public testing::Test
{

protected:

  const RoConfig cfg;
  const SafeZones sz;

  SafeZonesTests ()
    : cfg(xaya::Chain::REGTEST), sz(cfg)
  {}

};

TEST_F (SafeZonesTests, IsNoCombat)
{
  EXPECT_TRUE (sz.IsNoCombat (NEUTRAL));
  EXPECT_TRUE (sz.IsNoCombat (RED_START));
  EXPECT_FALSE (sz.IsNoCombat (NORMAL));
}

TEST_F (SafeZonesTests, StarterFor)
{
  EXPECT_EQ (sz.StarterFor (NEUTRAL), Faction::INVALID);
  EXPECT_EQ (sz.StarterFor (NORMAL), Faction::INVALID);
  EXPECT_EQ (sz.StarterFor (RED_START), Faction::RED);
}

/**
 * Exhaustively check each coordinate against the StarterZones and the
 * direct roconfig proto data.
 */
TEST_F (SafeZonesTests, Exhaustive)
{
  using namespace tiledata;
  for (HexCoord::IntT y = minY; y <= maxY; ++y)
    {
      const auto yInd = y - minY;
      for (HexCoord::IntT x = minX[yInd]; x <= maxX[yInd]; ++x)
        {
          const HexCoord c(x, y);

          const proto::SafeZone* zone = nullptr;
          for (const auto& pb : cfg->safe_zones ())
            {
              const HexCoord centre(pb.centre ().x (), pb.centre ().y ());
              const unsigned dist = HexCoord::DistanceL1 (c, centre);
              if (dist > pb.radius ())
                continue;

              ASSERT_EQ (zone, nullptr) << "Overlapping zones at " << c;
              zone = &pb;
            }

          ASSERT_EQ (sz.IsNoCombat (c), zone != nullptr);

          std::string zoneFaction;
          if (zone != nullptr && zone->has_faction ())
            zoneFaction = zone->faction ();

          const auto f = sz.StarterFor (c);
          switch (f)
            {
            case Faction::RED:
            case Faction::GREEN:
            case Faction::BLUE:
              ASSERT_EQ (zoneFaction, FactionToString (f));
              break;
            case Faction::INVALID:
              ASSERT_EQ (zoneFaction, "");
              break;
            default:
              FAIL ()
                  << "Unexpected result from StarterFor: "
                  << static_cast<int> (f);
              break;
            }
        }
    }
}

} // anonymous namespace
} // namespace pxd
