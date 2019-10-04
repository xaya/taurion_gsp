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

#include "params.hpp"
#include "testutils.hpp"

#include "database/dbtest.hpp"
#include "database/prizes.hpp"
#include "hexagonal/coord.hpp"

#include <gtest/gtest.h>

#include <set>

namespace pxd
{
namespace
{

/** Timestamp before the competition end.  */
constexpr int64_t TIME_IN_COMPETITION = 1571148000;
/** Timestamp after the competition end.  */
constexpr int64_t TIME_AFTER_COMPETITION = TIME_IN_COMPETITION + 1;

class ProspectingTests : public DBTestWithSchema
{

protected:

  /** Character table used for interacting with the test database.  */
  CharacterTable characters;

  /** Regions table for the test.  */
  RegionsTable regions;

  /** Random instance for prospecting prize tests.  */
  TestRandom rnd;

  /** Params instance.  Set to regtest, so we use the regtest prizes.  */
  const Params params;

  /** Basemap instance for testing.  */
  const BaseMap map;

  ProspectingTests ()
    : characters(db), regions(db), params(xaya::Chain::REGTEST)
  {
    InitialisePrizes (db, params);

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
  Prospect (CharacterTable::Handle c, const HexCoord& pos,
            const unsigned height, const int64_t timestamp)
  {
    const auto id = c->GetId ();
    c->SetPosition (pos);
    c->SetBusy (1);
    c->MutableProto ().mutable_prospection ();
    c.reset ();

    const auto region = map.Regions ().GetRegionId (pos);
    regions.GetById (region)->MutableProto ().set_prospecting_character (id);

    FinishProspecting (*characters.GetById (id), db, regions, rnd,
                       height, timestamp, params, map);
    return region;
  }

};

TEST_F (ProspectingTests, Basic)
{
  const auto region = Prospect (GetTest (), HexCoord (10, -20),
                                10, TIME_IN_COMPETITION);

  auto c = GetTest ();
  EXPECT_EQ (c->GetBusy (), 0);
  EXPECT_FALSE (c->GetProto ().has_prospection ());

  auto r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "domob");
  EXPECT_EQ (r->GetProto ().prospection ().height (), 10);
}

TEST_F (ProspectingTests, Prizes)
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
          const auto region = Prospect (std::move (c), pos,
                                        10, TIME_IN_COMPETITION);
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
  for (const auto& p : params.ProspectingPrizes ())
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

TEST_F (ProspectingTests, NoPrizesAfterEnd)
{
  constexpr unsigned trials = 1000;
  constexpr unsigned perRow = 10;

  for (unsigned i = 0; i < trials; i += perRow)
    {
      const HexCoord::IntT x = 2 * i;
      for (unsigned j = 0; j < perRow; ++j)
        {
          const HexCoord pos(x, 20 * j);
          auto c = characters.CreateNew ("domob", Faction::RED);
          Prospect (std::move (c), pos, 10, TIME_AFTER_COMPETITION);
        }
    }

  Prizes prizeTable(db);
  for (const auto& p : params.ProspectingPrizes ())
    EXPECT_EQ (prizeTable.GetFound (p.name), 0);
}

} // anonymous namespace
} // namespace pxd
