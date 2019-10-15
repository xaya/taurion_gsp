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

#include "mining.hpp"

#include "testutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/region.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class MiningTests : public DBTestWithSchema
{

protected:

  CharacterTable characters;
  RegionsTable regions;

  ContextForTesting ctx;

  const HexCoord pos;
  const RegionMap::IdT region;

  MiningTests ()
    : characters(db), regions(db),
      pos(-10, 42), region(ctx.Map ().Regions ().GetRegionId (pos))
  {
    auto c = GetTest ();
    c->SetPosition (pos);
    c->MutableProto ().mutable_mining ()->set_rate (10);
    c->MutableProto ().mutable_mining ()->set_active (true);
    c->MutableProto ().set_cargo_space (1000);
    c.reset ();

    auto r = GetRegion ();
    r->MutableProto ().mutable_prospection ()->set_resource ("foo");
    r->SetResourceLeft (100);
    r.reset ();
  }

  /**
   * Returns a test character (with ID 1).
   */
  CharacterTable::Handle
  GetTest ()
  {
    auto c = characters.GetById (1);
    if (c != nullptr)
      return c;

    c = characters.CreateNew ("domob", Faction::RED);
    CHECK_EQ (c->GetId (), 1);
    return c;
  }

  /**
   * Returns a handle to our test region.
   */
  RegionsTable::Handle
  GetRegion ()
  {
    return regions.GetById (region);
  }

};

TEST_F (MiningTests, Basic)
{
  ProcessAllMining (db, ctx);
  EXPECT_TRUE (GetTest ()->GetProto ().mining ().active ());
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("foo"), 10);
  EXPECT_EQ (GetRegion ()->GetResourceLeft (), 90);
}

TEST_F (MiningTests, NoMiningData)
{
  GetTest ()->MutableProto ().clear_mining ();
  ProcessAllMining (db, ctx);
  EXPECT_TRUE (GetTest ()->GetInventory ().IsEmpty ());
  EXPECT_EQ (GetRegion ()->GetResourceLeft (), 100);
}

TEST_F (MiningTests, MiningNotActive)
{
  GetTest ()->MutableProto ().mutable_mining ()->set_active (false);
  ProcessAllMining (db, ctx);
  EXPECT_TRUE (GetTest ()->GetInventory ().IsEmpty ());
  EXPECT_EQ (GetRegion ()->GetResourceLeft (), 100);
}

TEST_F (MiningTests, RegionNotProspected)
{
  GetRegion ()->MutableProto ().clear_prospection ();
  ProcessAllMining (db, ctx);
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
  EXPECT_TRUE (GetTest ()->GetInventory ().IsEmpty ());
}

TEST_F (MiningTests, ResourceUsedUp)
{
  GetRegion ()->SetResourceLeft (5);

  ProcessAllMining (db, ctx);
  EXPECT_TRUE (GetTest ()->GetProto ().mining ().active ());
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("foo"), 5);
  EXPECT_EQ (GetRegion ()->GetResourceLeft (), 0);

  ProcessAllMining (db, ctx);
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("foo"), 5);
  EXPECT_EQ (GetRegion ()->GetResourceLeft (), 0);
}

TEST_F (MiningTests, CargoFull)
{
  GetTest ()->GetInventory ().SetFungibleCount ("foo", 95);

  ProcessAllMining (db, ctx);
  EXPECT_TRUE (GetTest ()->GetProto ().mining ().active ());
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("foo"), 100);
  EXPECT_EQ (GetRegion ()->GetResourceLeft (), 95);

  ProcessAllMining (db, ctx);
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("foo"), 100);
  EXPECT_EQ (GetRegion ()->GetResourceLeft (), 95);
}

TEST_F (MiningTests, MultipleMiners)
{
  GetRegion ()->SetResourceLeft (25);

  auto c = characters.CreateNew ("andy", Faction::BLUE);
  ASSERT_EQ (c->GetId (), 2);
  c->SetPosition (pos);
  c->MutableProto ().mutable_mining ()->set_rate (10);
  c->MutableProto ().mutable_mining ()->set_active (true);
  c->MutableProto ().set_cargo_space (1000);
  c.reset ();

  /* In the first turn, both miners will be able to mine at their rate.  In the
     second, the resource will get used up and only the first one will get
     something (but not at full rate).  */
  ProcessAllMining (db, ctx);
  ProcessAllMining (db, ctx);

  EXPECT_TRUE (GetTest ()->GetProto ().mining ().active ());
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("foo"), 15);

  c = characters.GetById (2);
  EXPECT_FALSE (c->GetProto ().mining ().active ());
  EXPECT_EQ (c->GetInventory ().GetFungibleCount ("foo"), 10);
  c.reset ();

  EXPECT_EQ (GetRegion ()->GetResourceLeft (), 0);
}

} // anonymous namespace
} // namespace pxd
