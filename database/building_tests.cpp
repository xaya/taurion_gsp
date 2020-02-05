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

#include "building.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class BuildingTests : public DBTestWithSchema
{

protected:

  BuildingsTable tbl;

  BuildingTests ()
    : tbl(db)
  {}

};

TEST_F (BuildingTests, Creation)
{
  const HexCoord pos(5, -2);

  auto h = tbl.CreateNew  ("refinery", "", Faction::ANCIENT);
  h->SetCentre (pos);
  const auto id1 = h->GetId ();
  h.reset ();

  h = tbl.CreateNew ("turret", "andy", Faction::GREEN);
  const auto id2 = h->GetId ();
  h->MutableProto ().mutable_shape_trafo ()->set_rotation_steps (3);
  h.reset ();

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  ASSERT_EQ (h->GetId (), id1);
  EXPECT_EQ (h->GetType (), "refinery");
  EXPECT_EQ (h->GetFaction (), Faction::ANCIENT);
  EXPECT_EQ (h->GetCentre (), pos);
  EXPECT_FALSE (h->GetProto ().has_shape_trafo ());

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  ASSERT_EQ (h->GetId (), id2);
  EXPECT_EQ (h->GetType (), "turret");
  EXPECT_EQ (h->GetOwner (), "andy");
  EXPECT_EQ (h->GetFaction (), Faction::GREEN);
  EXPECT_EQ (h->GetCentre (), HexCoord (0, 0));
  EXPECT_EQ (h->GetProto ().shape_trafo ().rotation_steps (), 3);

  ASSERT_FALSE (res.Step ());
}

TEST_F (BuildingTests, ModificationOfProto)
{
  const HexCoord pos(5, -2);
  auto h = tbl.CreateNew ("refinery", "domob", Faction::RED);
  const auto id = h->GetId ();
  h->SetCentre (pos);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_FALSE (h->GetProto ().has_shape_trafo ());
  h->MutableProto ().mutable_shape_trafo ()->set_rotation_steps (4);
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_EQ (h->GetType (), "refinery");
  EXPECT_EQ (h->GetOwner (), "domob");
  EXPECT_EQ (h->GetFaction (), Faction::RED);
  EXPECT_EQ (h->GetCentre (), pos);
  EXPECT_EQ (h->GetProto ().shape_trafo ().rotation_steps (), 4);
}

TEST_F (BuildingTests, ModificationOfFields)
{
  const auto id = tbl.CreateNew ("refinery", "domob", Faction::RED)->GetId ();

  auto h = tbl.GetById (id);
  EXPECT_EQ (h->GetOwner (), "domob");
  h->SetOwner ("andy");
  h.reset ();

  h = tbl.GetById (id);
  EXPECT_EQ (h->GetType (), "refinery");
  EXPECT_EQ (h->GetOwner (), "andy");
  EXPECT_EQ (h->GetFaction (), Faction::RED);
}

/* ************************************************************************** */

using BuildingsTableTests = BuildingTests;

TEST_F (BuildingsTableTests, GetById)
{
  const auto id1 = tbl.CreateNew ("turret", "domob", Faction::RED)->GetId ();
  const auto id2 = tbl.CreateNew ("turret", "andy", Faction::RED)->GetId ();

  CHECK (tbl.GetById (500) == nullptr);
  CHECK_EQ (tbl.GetById (id1)->GetOwner (), "domob");
  CHECK_EQ (tbl.GetById (id2)->GetOwner (), "andy");
}

TEST_F (BuildingsTableTests, QueryAll)
{
  tbl.CreateNew ("turret", "domob", Faction::RED);
  tbl.CreateNew ("turret", "andy", Faction::RED);

  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "domob");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetOwner (), "andy");
  ASSERT_FALSE (res.Step ());
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
