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

#include "inventory.hpp"

#include "dbtest.hpp"

#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

#include <limits>
#include <map>

namespace pxd
{
namespace
{

using google::protobuf::TextFormat;

/* ************************************************************************** */

TEST (QuantityProductTests, Initialisation)
{
  EXPECT_EQ (QuantityProduct ().Extract (), 0);
  EXPECT_EQ (QuantityProduct (1, 0).Extract (), 0);
  EXPECT_EQ (QuantityProduct (6, 7).Extract (), 42);
  EXPECT_EQ (QuantityProduct (-5, 4).Extract (), -20);
  EXPECT_EQ (QuantityProduct (1'000'000'000'000ll, -2).Extract (),
             -2'000'000'000'000ll);
}

TEST (QuantityProductTests, AddProduct)
{
  QuantityProduct total;
  total.AddProduct (6, 7);
  total.AddProduct (-2, 5);
  total.AddProduct (1'000'000'000, 1'000'000'000);
  EXPECT_EQ (total.Extract (), 42 - 10 + 1'000'000'000'000'000'000ll);
}

TEST (QuantityProductTests, Comparison)
{
  QuantityProduct pos(10, 10);
  EXPECT_TRUE (pos <= 100);
  EXPECT_TRUE (pos <= 1'000'000'000'000);
  EXPECT_FALSE (pos <= 99);
  EXPECT_FALSE (pos > 100);
  EXPECT_TRUE (pos > 99);

  QuantityProduct zero;
  EXPECT_TRUE (zero <= 0);
  EXPECT_TRUE (zero <= 100);

  QuantityProduct neg(-1, 1);
  EXPECT_TRUE (neg <= 0);
  EXPECT_TRUE (neg <= 100);
}

TEST (QuantityProductTests, Overflows)
{
  QuantityProduct pos(std::numeric_limits<int64_t>::max (),
                      std::numeric_limits<int64_t>::max ());
  EXPECT_FALSE (pos <= std::numeric_limits<int64_t>::max ());
  EXPECT_DEATH (pos.Extract (), "is too large");

  QuantityProduct neg(std::numeric_limits<int64_t>::max (),
                      -std::numeric_limits<int64_t>::max ());
  EXPECT_TRUE (neg <= 0);
  EXPECT_TRUE (neg <= std::numeric_limits<int64_t>::max ());
  EXPECT_DEATH (neg.Extract (), "is too large");
}

/* ************************************************************************** */

class InventoryTests : public testing::Test
{

protected:

  Inventory inv;

  /**
   * Expects that the elements in the fungible "map" match the given
   * set of expected elements.
   */
  void
  ExpectFungibleElements (const std::map<std::string, uint64_t>& expected)
  {
    const auto& fungible = inv.GetFungible ();
    ASSERT_EQ (expected.size (), fungible.size ());
    for (const auto& entry : expected)
      {
        const auto mit = fungible.find (entry.first);
        ASSERT_TRUE (mit != fungible.end ())
            << "Entry " << entry.first << " not found in actual data";
        ASSERT_EQ (mit->second, entry.second);
      }
  }

};

TEST_F (InventoryTests, DefaultData)
{
  ExpectFungibleElements ({});
  EXPECT_EQ (inv.GetFungibleCount ("foo"), 0);
  EXPECT_FALSE (inv.IsDirty ());
  EXPECT_EQ (inv.GetProtoForBinding ().GetSerialised (), "");
}

TEST_F (InventoryTests, FromProto)
{
  LazyProto<proto::Inventory> pb;
  pb.SetToDefault ();
  CHECK (TextFormat::ParseFromString (R"(
    fungible: { key: "foo" value: 10 }
    fungible: { key: "bar" value: 5 }
  )", &pb.Mutable ()));

  inv = std::move (pb);

  ExpectFungibleElements ({{"foo", 10}, {"bar", 5}});
  EXPECT_FALSE (inv.IsEmpty ());
}

TEST_F (InventoryTests, Modification)
{
  inv.SetFungibleCount ("foo", 10);
  inv.SetFungibleCount ("bar", 5);

  ExpectFungibleElements ({{"foo", 10}, {"bar", 5}});
  EXPECT_EQ (inv.GetFungibleCount ("foo"), 10);
  EXPECT_EQ (inv.GetFungibleCount ("bar"), 5);
  EXPECT_EQ (inv.GetFungibleCount ("baz"), 0);
  EXPECT_FALSE (inv.IsEmpty ());
  EXPECT_TRUE (inv.IsDirty ());

  inv.AddFungibleCount ("bar", 3);
  ExpectFungibleElements ({{"foo", 10}, {"bar", 8}});

  inv.SetFungibleCount ("foo", 0);
  ExpectFungibleElements ({{"bar", 8}});
  EXPECT_FALSE (inv.IsEmpty ());

  inv.AddFungibleCount ("bar", -8);
  EXPECT_TRUE (inv.IsEmpty ());
  EXPECT_TRUE (inv.IsDirty ());
}

TEST_F (InventoryTests, Equality)
{
  Inventory a, b, c;

  a.SetFungibleCount ("foo", 10);
  a.SetFungibleCount ("bar", 5);

  b.SetFungibleCount ("bar", 5);
  b.SetFungibleCount ("foo", 10);

  c.SetFungibleCount ("foo", 10);
  c.SetFungibleCount ("bar", 5);
  c.SetFungibleCount ("zerospace", 1);

  EXPECT_EQ (a, a);
  EXPECT_EQ (a, b);
  EXPECT_NE (a, c);
}

TEST_F (InventoryTests, Clear)
{
  inv.SetFungibleCount ("foo", 10);
  inv.SetFungibleCount ("bar", 5);

  /* Clearing twice should be fine (and just not have any effect).  */
  inv.Clear ();
  inv.Clear ();

  EXPECT_TRUE (inv.IsEmpty ());
  EXPECT_EQ (inv.GetFungibleCount ("foo"), 0);
}

TEST_F (InventoryTests, AdditionOfOtherInventory)
{
  inv.SetFungibleCount ("foo", 10);

  Inventory other;
  other.SetFungibleCount ("foo", 2);
  other.SetFungibleCount ("bar", 3);

  inv += other;

  EXPECT_EQ (inv.GetFungibleCount ("foo"), 12);
  EXPECT_EQ (inv.GetFungibleCount ("bar"), 3);
}

TEST_F (InventoryTests, ProtoRef)
{
  proto::Inventory pb;
  pb.mutable_fungible ()->insert ({"foo", 5});

  Inventory other(pb);
  EXPECT_EQ (other.GetFungibleCount ("foo"), 5);
  other.AddFungibleCount ("foo", -5);
  other.AddFungibleCount ("bar", 10);

  EXPECT_EQ (pb.fungible ().size (), 1);
  EXPECT_EQ (pb.fungible ().at ("bar"), 10);

  const proto::Inventory& constRef(pb);
  Inventory ro(constRef);
  EXPECT_EQ (ro.GetFungibleCount ("bar"), 10);
  EXPECT_DEATH (ro.AddFungibleCount ("foo", 1), "non-mutable");
}

/* ************************************************************************** */

struct CountResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, cnt, 1);
};

/**
 * General test fixture with functionality for testing inventories, either
 * on the ground or associated to a building/account.
 */
class InventoryRowTests : public DBTestWithSchema
{

protected:

  /**
   * Returns the number of entries in the inventory database table.
   */
  unsigned
  CountEntries (const std::string& tbl)
  {
    auto stmt = db.Prepare (R"(
      SELECT COUNT(*) AS `cnt`
        FROM `)" + tbl + R"(`
    )");
    auto res = stmt.Query<CountResult> ();
    CHECK (res.Step ());
    const unsigned cnt = res.Get<CountResult::cnt> ();
    CHECK (!res.Step ());
    return cnt;
  }

};

class GroundLootTests : public InventoryRowTests
{

protected:

  GroundLootTable tbl;

  GroundLootTests ()
    : tbl(db)
  {}

  unsigned
  CountEntries ()
  {
    return InventoryRowTests::CountEntries ("ground_loot");
  }

};

TEST_F (GroundLootTests, DefaultData)
{
  auto h = tbl.GetByCoord (HexCoord (1, 2));
  EXPECT_EQ (h->GetPosition (), HexCoord (1, 2));
  EXPECT_TRUE (h->GetInventory ().IsEmpty ());
  h.reset ();

  EXPECT_EQ (CountEntries (), 0);
}

TEST_F (GroundLootTests, Update)
{
  const HexCoord c1(1, 2);
  tbl.GetByCoord (c1)->GetInventory ().SetFungibleCount ("foo", 5);

  const HexCoord c2(1, 3);
  tbl.GetByCoord (c2)->GetInventory ().SetFungibleCount ("bar", 42);

  auto h = tbl.GetByCoord (c1);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 5);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), 0);

  h = tbl.GetByCoord (c2);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 0);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), 42);

  EXPECT_EQ (CountEntries (), 2);
}

TEST_F (GroundLootTests, Removal)
{
  const HexCoord c(1, 2);
  tbl.GetByCoord (c)->GetInventory ().SetFungibleCount ("foo", 5);
  EXPECT_EQ (CountEntries (), 1);

  auto h = tbl.GetByCoord (c);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 5);
  h->GetInventory ().SetFungibleCount ("foo", 0);
  h.reset ();
  EXPECT_EQ (CountEntries (), 0);

  h = tbl.GetByCoord (c);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 0);
  EXPECT_TRUE (h->GetInventory ().IsEmpty ());
}

using GroundLootTableTests = GroundLootTests;

TEST_F (GroundLootTableTests, QueryNonEmpty)
{
  const HexCoord c1(1, 2);
  const HexCoord c2(1, 3);
  const HexCoord c3(2, 2);

  tbl.GetByCoord (c1)->GetInventory ().SetFungibleCount ("foo", 1);
  tbl.GetByCoord (c2)->GetInventory ().SetFungibleCount ("foo", 2);
  tbl.GetByCoord (c3)->GetInventory ().SetFungibleCount ("foo", 3);

  auto res = tbl.QueryNonEmpty ();

  ASSERT_TRUE (res.Step ());
  auto h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetPosition (), c1);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 1);

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetPosition (), c2);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 2);

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetPosition (), c3);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 3);

  ASSERT_FALSE (res.Step ());
}

/* ************************************************************************** */

class BuildingInventoryTests : public InventoryRowTests
{

protected:

  BuildingInventoriesTable tbl;

  BuildingInventoryTests ()
    : tbl(db)
  {}

  unsigned
  CountEntries ()
  {
    return InventoryRowTests::CountEntries ("building_inventories");
  }

};

TEST_F (BuildingInventoryTests, DefaultData)
{
  auto h = tbl.Get (123, "domob");
  EXPECT_EQ (h->GetBuildingId (), 123);
  EXPECT_EQ (h->GetAccount (), "domob");
  EXPECT_TRUE (h->GetInventory ().IsEmpty ());
  h.reset ();

  EXPECT_EQ (CountEntries (), 0);
}

TEST_F (BuildingInventoryTests, Update)
{
  tbl.Get (123, "domob")->GetInventory ().SetFungibleCount ("foo", 5);
  tbl.Get (123, "andy")->GetInventory ().SetFungibleCount ("bar", 42);
  tbl.Get (124, "domob")->GetInventory ().SetFungibleCount ("bar", 10);

  auto h = tbl.Get (123, "domob");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 5);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), 0);

  h = tbl.Get (123, "andy");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 0);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), 42);

  h = tbl.Get (124, "domob");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 0);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), 10);

  EXPECT_EQ (CountEntries (), 3);
}

TEST_F (BuildingInventoryTests, Removal)
{
  tbl.Get (123, "domob")->GetInventory ().SetFungibleCount ("foo", 5);
  EXPECT_EQ (CountEntries (), 1);

  auto h = tbl.Get (123, "domob");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 5);
  h->GetInventory ().SetFungibleCount ("foo", 0);
  h.reset ();
  EXPECT_EQ (CountEntries (), 0);

  h = tbl.Get (123, "domob");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 0);
  EXPECT_TRUE (h->GetInventory ().IsEmpty ());
}

using BuildingInventoriesTableTests = BuildingInventoryTests;

TEST_F (BuildingInventoriesTableTests, QueryAll)
{
  tbl.Get (123, "domob")->GetInventory ().SetFungibleCount ("foo", 1);
  tbl.Get (124, "andy")->GetInventory ().SetFungibleCount ("foo", 2);

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetBuildingId (), 123);
  EXPECT_EQ (h->GetAccount (), "domob");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 1);

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetBuildingId (), 124);
  EXPECT_EQ (h->GetAccount (), "andy");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 2);

  ASSERT_FALSE (res.Step ());
}

TEST_F (BuildingInventoriesTableTests, QueryForBuilding)
{
  tbl.Get (123, "domob")->GetInventory ().SetFungibleCount ("foo", 1);
  tbl.Get (124, "domob")->GetInventory ().SetFungibleCount ("foo", 2);
  tbl.Get (123, "andy")->GetInventory ().SetFungibleCount ("foo", 3);

  auto res = tbl.QueryForBuilding (125);
  ASSERT_FALSE (res.Step ());
  res = tbl.QueryForBuilding (123);

  ASSERT_TRUE (res.Step ());
  auto h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetBuildingId (), 123);
  EXPECT_EQ (h->GetAccount (), "andy");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 3);

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetBuildingId (), 123);
  EXPECT_EQ (h->GetAccount (), "domob");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 1);

  ASSERT_FALSE (res.Step ());
}

TEST_F (BuildingInventoriesTableTests, RemoveBuilding)
{
  tbl.Get (123, "domob")->GetInventory ().SetFungibleCount ("foo", 1);
  tbl.Get (124, "domob")->GetInventory ().SetFungibleCount ("foo", 2);
  tbl.Get (123, "andy")->GetInventory ().SetFungibleCount ("foo", 3);

  tbl.RemoveBuilding (123);

  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetBuildingId (), 124);
  EXPECT_EQ (h->GetAccount (), "domob");
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 2);
  EXPECT_FALSE (res.Step ());
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
