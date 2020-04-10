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

#include "lazyproto.hpp"

#include "hexagonal/coord.hpp"
#include "proto/geometry.pb.h"

#include <gtest/gtest.h>

namespace pxd
{

class LazyProtoTests : public testing::Test
{

protected:

  LazyProto<proto::HexCoord> lazy;

  /**
   * Sets our LazyProto instance based on the given coordinate.
   */
  void
  SetToCoord (const HexCoord& c)
  {
    proto::HexCoord pb;
    pb.set_x (c.GetX ());
    pb.set_y (c.GetY ());

    std::string bytes;
    CHECK (pb.SerializeToString (&bytes));

    lazy = LazyProto<proto::HexCoord> (std::move (bytes));
  }

  /**
   * Checks if the protocol buffer has been parsed.
   */
  bool
  IsProtoParsed () const
  {
    return lazy.msg.has_x () || lazy.msg.has_y ();
  }

  /**
   * Checks that the protocol buffer is not serialised on demand for
   * GetSerialised, but that a serialised string is known already and returned.
   * We do this by modifying the proto message and checking that the serialised
   * string does not change.
   */
  bool
  IsSerialisationCached ()
  {
    const std::string before = lazy.GetSerialised ();
    lazy.msg.set_x (-12345);
    const std::string after = lazy.GetSerialised ();

    return before == after;
  }

};

namespace
{

TEST_F (LazyProtoTests, SetToDefault)
{
  SetToCoord (HexCoord (42, -5));

  lazy.SetToDefault ();
  EXPECT_EQ (lazy.GetSerialised (), "");
  EXPECT_FALSE (lazy.Get ().has_x ());
  EXPECT_FALSE (lazy.Get ().has_y ());

  EXPECT_FALSE (lazy.IsDirty ());
  EXPECT_TRUE (IsSerialisationCached ());
}

TEST_F (LazyProtoTests, ProtoNotParsed)
{
  SetToCoord (HexCoord (42, -5));
  const std::string bytes = lazy.GetSerialised ();

  EXPECT_FALSE (lazy.IsDirty ());
  EXPECT_FALSE (IsProtoParsed ());
  EXPECT_TRUE (IsSerialisationCached ());

  proto::HexCoord pb;
  ASSERT_TRUE (pb.ParseFromString (bytes));
  EXPECT_EQ (pb.x (), 42);
  EXPECT_EQ (pb.y (), -5);
}

TEST_F (LazyProtoTests, ProtoNotModified)
{
  SetToCoord (HexCoord (42, -5));

  EXPECT_EQ (lazy.Get ().x (), 42);
  EXPECT_EQ (lazy.Get ().y (), -5);

  EXPECT_FALSE (lazy.IsDirty ());
  EXPECT_TRUE (IsProtoParsed ());
  EXPECT_TRUE (IsSerialisationCached ());
}

TEST_F (LazyProtoTests, ProtoModified)
{
  SetToCoord (HexCoord (42, -5));
  lazy.Mutable ().set_x (-10);
  const std::string bytes = lazy.GetSerialised ();

  EXPECT_EQ (lazy.Get ().x (), -10);
  EXPECT_EQ (lazy.Get ().y (), -5);

  EXPECT_TRUE (lazy.IsDirty ());
  EXPECT_TRUE (IsProtoParsed ());
  EXPECT_FALSE (IsSerialisationCached ());

  proto::HexCoord pb;
  ASSERT_TRUE (pb.ParseFromString (bytes));
  EXPECT_EQ (pb.x (), -10);
  EXPECT_EQ (pb.y (), -5);
}

TEST_F (LazyProtoTests, IsEmpty)
{
  SetToCoord (HexCoord (42, -5));
  EXPECT_FALSE (lazy.IsEmpty ());

  lazy.SetToDefault ();
  lazy.Get ();
  EXPECT_TRUE (lazy.IsEmpty ());

  lazy.Mutable ();
  EXPECT_FALSE (lazy.IsEmpty ());
}

} // anonymous namespace
} // namespace pxd
