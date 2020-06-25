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

#include "prospecting.hpp"

#include "testutils.hpp"

#include "database/dbtest.hpp"
#include "database/itemcounts.hpp"
#include "hexagonal/coord.hpp"
#include "proto/roconfig.hpp"

#include <gtest/gtest.h>

#include <set>
#include <map>

namespace pxd
{
namespace
{

/** Position where prizes are won with normal chance.  */
const HexCoord POS_NORMAL_PRIZES(4'000, 0);
/** Position with low chance for prizes.  */
const HexCoord POS_LOW_PRIZES(1'000, 500);

/* ************************************************************************** */

class CanProspectRegionTests : public DBTestWithSchema
{

protected:

  CharacterTable characters;
  RegionsTable regions;

  ContextForTesting ctx;

  const HexCoord pos;
  const RegionMap::IdT region;

  CanProspectRegionTests ()
    : characters(db), regions(db, 1'042),
      pos(-10, 42), region(ctx.Map ().Regions ().GetRegionId (pos))
  {}

};

TEST_F (CanProspectRegionTests, ProspectionInProgress)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  auto r = regions.GetById (region);
  r->MutableProto ().set_prospecting_character (10);

  EXPECT_FALSE (CanProspectRegion (*c, *r, ctx));
}

TEST_F (CanProspectRegionTests, EmptyRegion)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  auto r = regions.GetById (region);

  EXPECT_TRUE (CanProspectRegion (*c, *r, ctx));
}

TEST_F (CanProspectRegionTests, ReprospectingExpiration)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  auto r = regions.GetById (region);
  r->MutableProto ().mutable_prospection ()->set_height (1);

  ctx.SetHeight (100);
  EXPECT_FALSE (CanProspectRegion (*c, *r, ctx));

  ctx.SetHeight (101);
  EXPECT_TRUE (CanProspectRegion (*c, *r, ctx));
}

TEST_F (CanProspectRegionTests, ReprospectingResources)
{
  ctx.SetHeight (1000);

  auto c = characters.CreateNew ("domob", Faction::RED);
  auto r = regions.GetById (region);
  r->MutableProto ().mutable_prospection ()->set_height (1);
  r->MutableProto ().mutable_prospection ()->set_resource ("foo");

  r->SetResourceLeft (1);
  EXPECT_FALSE (CanProspectRegion (*c, *r, ctx));

  r->SetResourceLeft (0);
  EXPECT_TRUE (CanProspectRegion (*c, *r, ctx));
}

/* ************************************************************************** */

class FinishProspectingTests : public DBTestWithSchema
{

protected:

  CharacterTable characters;
  RegionsTable regions;

  TestRandom rnd;

  ContextForTesting ctx;

  FinishProspectingTests ()
    : characters(db), regions(db, 1'042)
  {
    const auto h = characters.CreateNew ("domob", Faction::RED);
    CHECK_EQ (h->GetId (), 1);
  }

  /**
   * Returns a handle to the test character (for inspection and update).
   */
  CharacterTable::Handle
  GetTest ()
  {
    return characters.GetById (1);
  }

  /**
   * Prospects with the given character on the given location.  This sets up
   * the character on that position and calls FinishProspecting.
   *
   * Returns the region ID prospected.
   */
  RegionMap::IdT
  Prospect (CharacterTable::Handle c, const HexCoord& pos)
  {
    const auto id = c->GetId ();
    c->SetPosition (pos);
    c.reset ();

    const auto region = ctx.Map ().Regions ().GetRegionId (pos);
    regions.GetById (region)->MutableProto ().set_prospecting_character (id);

    FinishProspecting (*characters.GetById (id), db, regions, rnd, ctx);
    return region;
  }

  /**
   * Prospects with the given character on the given location and afterwards
   * clears the region prospection again.  This is useful for testing prizes
   * (which will remain in the character inventory afterwards).
   */
  void
  ProspectAndClear (CharacterTable::Handle c, const HexCoord& pos)
  {
    const auto regionId = Prospect (std::move (c), pos);

    auto r = regions.GetById (regionId);
    ASSERT_TRUE (r->GetProto ().has_prospection ());
    r->MutableProto ().clear_prospection ();
  }

};

TEST_F (FinishProspectingTests, Basic)
{
  ctx.SetHeight (10);
  const auto region = Prospect (GetTest (), HexCoord (10, -20));

  auto r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "domob");
  EXPECT_EQ (r->GetProto ().prospection ().height (), 10);
}

TEST_F (FinishProspectingTests, Resources)
{
  std::map<std::string, unsigned> regionsForResource;
  for (int i = -30; i < 30; ++i)
    for (int j = -30; j < 30; ++j)
      {
        const HexCoord pos(100 * i, 100 * j);
        if (!ctx.Map ().IsOnMap (pos) || !ctx.Map ().IsPassable (pos))
          continue;

        auto c = characters.CreateNew ("domob", Faction::RED);
        const auto id = Prospect (std::move (c), pos);

        auto r = regions.GetById (id);
        EXPECT_GT (r->GetResourceLeft (), 0);
        ++regionsForResource[r->GetProto ().prospection ().resource ()];
      }

  for (const auto& entry : regionsForResource)
    LOG (INFO)
        << "Found resource " << entry.first
        << " in " << entry.second << " regions";

  ASSERT_EQ (regionsForResource.size (), 9);
  EXPECT_GT (regionsForResource["raw a"], regionsForResource["raw i"]);
}

TEST_F (FinishProspectingTests, Prizes)
{
  constexpr unsigned trials = 10'000;

  ASSERT_FALSE (ctx.Params ().IsLowPrizeZone (POS_NORMAL_PRIZES));
  ASSERT_TRUE (ctx.Map ().IsPassable (POS_NORMAL_PRIZES));

  const auto id = characters.CreateNew ("domob", Faction::RED)->GetId ();

  for (unsigned i = 0; i < trials; ++i)
    ProspectAndClear (characters.GetById (id), POS_NORMAL_PRIZES);

  std::map<std::string, unsigned> foundMap;
  auto c = characters.GetById (id);
  for (const auto& item : c->GetInventory ().GetFungible ())
    {
      constexpr const char* suffix = " prize";
      const auto ind = item.first.find (suffix);
      if (ind == std::string::npos)
        continue;
      foundMap[item.first.substr (0, ind)] += item.second;
    }
  c.reset ();

  ItemCounts cnt(db);
  for (const auto& p : ctx.RoConfig ()->params ().prizes ())
    {
      LOG (INFO)
          << "Found for prize " << p.name () << ": " << foundMap[p.name ()];
      EXPECT_EQ (cnt.GetFound (p.name () + " prize"), foundMap[p.name ()]);
    }

  /* We should have found all gold prizes (since there are only a few),
     the one bronze prize and roughly the expected number
     of silver prizes by probability.  */
  EXPECT_EQ (foundMap["gold"], 3);
  EXPECT_EQ (foundMap["bronze"], 1);
  /* Expected value is 1000.  */
  EXPECT_GE (foundMap["silver"], 950);
  EXPECT_LE (foundMap["silver"], 1050);
}

TEST_F (FinishProspectingTests, FewerPrizesInCentre)
{
  constexpr unsigned trials = 10'000;

  ASSERT_TRUE (ctx.Params ().IsLowPrizeZone (POS_LOW_PRIZES));
  ASSERT_TRUE (ctx.Map ().IsPassable (POS_LOW_PRIZES));

  const auto id = characters.CreateNew ("domob", Faction::RED)->GetId ();

  for (unsigned i = 0; i < trials; ++i)
    ProspectAndClear (characters.GetById (id), POS_LOW_PRIZES);

  ItemCounts cnt(db);
  const auto silver = cnt.GetFound ("silver prize");
  LOG (INFO) << "Found silver prizes in low-chance area: " << silver;
  /* Expected value is 550.  */
  EXPECT_GE (silver, 500);
  EXPECT_LE (silver, 600);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
