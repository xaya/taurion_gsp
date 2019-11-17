/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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
#include "database/prizes.hpp"
#include "hexagonal/coord.hpp"

#include <gtest/gtest.h>

#include <set>
#include <map>

namespace pxd
{
namespace
{

/** Timestamp before the competition end.  */
constexpr int64_t TIME_IN_COMPETITION = 1571148000;
/** Timestamp after the competition end.  */
constexpr int64_t TIME_AFTER_COMPETITION = TIME_IN_COMPETITION + 1;

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
    : characters(db), regions(db),
      pos(-10, 42), region(ctx.Map ().Regions ().GetRegionId (pos))
  {
    ctx.SetChain (xaya::Chain::REGTEST);
  }

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
    : characters(db), regions(db)
  {
    ctx.SetTimestamp (TIME_IN_COMPETITION);
    ctx.SetChain (xaya::Chain::REGTEST);
    InitialisePrizes (db, ctx.Params ());

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
   * the character on that position, marks it as prospecting, and calls
   * FinishProspecting.
   *
   * Returns the region ID prospected.
   */
  RegionMap::IdT
  Prospect (CharacterTable::Handle c, const HexCoord& pos)
  {
    const auto id = c->GetId ();
    c->SetPosition (pos);
    c->SetBusy (1);
    c->MutableProto ().mutable_prospection ();
    c.reset ();

    const auto region = ctx.Map ().Regions ().GetRegionId (pos);
    regions.GetById (region)->MutableProto ().set_prospecting_character (id);

    FinishProspecting (*characters.GetById (id), db, regions, rnd, ctx);
    return region;
  }

};

TEST_F (FinishProspectingTests, Basic)
{
  ctx.SetHeight (10);
  const auto region = Prospect (GetTest (), HexCoord (10, -20));

  auto c = GetTest ();
  EXPECT_EQ (c->GetBusy (), 0);
  EXPECT_FALSE (c->GetProto ().has_prospection ());

  auto r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "domob");
  EXPECT_EQ (r->GetProto ().prospection ().height (), 10);
}

TEST_F (FinishProspectingTests, Resources)
{
  BaseMap map;

  std::map<std::string, unsigned> regionsForResource;
  for (int i = -30; i < 30; ++i)
    for (int j = -30; j < 30; ++j)
      {
        const HexCoord pos(100 * i, 100 * j);
        if (!map.IsOnMap (pos) || !map.IsPassable (pos))
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
  constexpr unsigned trials = 1000;
  constexpr unsigned perRow = 10;

  std::set<RegionMap::IdT> regionIds;
  for (unsigned i = 0; i < trials; i += perRow)
    {
      const HexCoord::IntT x = 2 * i;
      for (unsigned j = 0; j < perRow; ++j)
        {
          const HexCoord pos(x, 20 * j);
          auto c = characters.CreateNew ("domob", Faction::RED);
          const auto region = Prospect (std::move (c), pos);
          const auto res = regionIds.insert (region);
          ASSERT_TRUE (res.second);
        }
    }

  ASSERT_EQ (regionIds.size (), trials);
  for (const auto id : regionIds)
    {
      auto r = regions.GetById (id);
      ASSERT_TRUE (r->GetProto ().has_prospection ());
    }

  std::map<std::string, unsigned> foundMap;
  auto res = characters.QueryAll ();
  while (res.Step ())
    {
      auto c = characters.GetFromResult (res);
      for (const auto& item : c->GetInventory ().GetFungible ())
        {
          constexpr const char* suffix = " prize";
          const auto ind = item.first.find (suffix);
          if (ind == std::string::npos)
            continue;
          foundMap[item.first.substr (0, ind)] += item.second;
        }
    }

  Prizes prizeTable(db);
  for (const auto& p : ctx.Params ().ProspectingPrizes ())
    {
      LOG (INFO) << "Found for prize " << p.name << ": " << foundMap[p.name];
      EXPECT_EQ (prizeTable.GetFound (p.name), foundMap[p.name]);
    }

  /* We should have found all gold prizes (since there are only a few),
     the one bronze prize and roughly the expected number
     of silver prizes by probability.  */
  EXPECT_EQ (foundMap["gold"], 3);
  EXPECT_GE (foundMap["silver"], 50);
  EXPECT_LE (foundMap["silver"], 150);
  EXPECT_EQ (foundMap["bronze"], 1);
}

TEST_F (FinishProspectingTests, NoPrizesAfterEnd)
{
  constexpr unsigned trials = 1000;
  constexpr unsigned perRow = 10;

  ctx.SetTimestamp (TIME_AFTER_COMPETITION);

  for (unsigned i = 0; i < trials; i += perRow)
    {
      const HexCoord::IntT x = 2 * i;
      for (unsigned j = 0; j < perRow; ++j)
        {
          const HexCoord pos(x, 20 * j);
          auto c = characters.CreateNew ("domob", Faction::RED);
          Prospect (std::move (c), pos);
        }
    }

  Prizes prizeTable(db);
  for (const auto& p : ctx.Params ().ProspectingPrizes ())
    EXPECT_EQ (prizeTable.GetFound (p.name), 0);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
