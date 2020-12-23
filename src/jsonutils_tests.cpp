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

#include "jsonutils.hpp"

#include "testutils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <glog/logging.h>

#include <limits>

namespace pxd
{
namespace
{

using testing::ElementsAre;

using JsonCoordTests = testing::Test;

TEST_F (JsonCoordTests, CoordToJson)
{
  const Json::Value val = CoordToJson (HexCoord (-5, 42));
  ASSERT_TRUE (val.isObject ());
  EXPECT_EQ (val.size (), 2);
  ASSERT_TRUE (val.isMember ("x"));
  ASSERT_TRUE (val.isMember ("y"));
  ASSERT_TRUE (val["x"].isInt64 ());
  ASSERT_TRUE (val["y"].isInt64 ());
  EXPECT_EQ (val["x"].asInt64 (), -5);
  EXPECT_EQ (val["y"].asInt64 (), 42);
}

TEST_F (JsonCoordTests, ValidCoordFromJson)
{
  HexCoord c;
  ASSERT_TRUE (CoordFromJson (ParseJson (R"(
    {
      "x": -5,
      "y": 42
    }
  )"), c));
  EXPECT_EQ (c, HexCoord (-5, 42));
}

TEST_F (JsonCoordTests, InvalidCoordFromJson)
{
  for (const auto& str : {"42", "true", R"("foo")", "[1,2,3]",
                          "{}", R"({"x": 5})", R"({"x": 1.5, "y": 42})",
                          R"({"x": 1.0, "y": 0})",
                          R"({"x": 1, "y": 2e2})",
                          R"({"x": -1, "y": 1000000000})",
                          R"({"x": 0, "y": 0, "foo": 0})"})
    {
      HexCoord c;
      EXPECT_FALSE (CoordFromJson (ParseJson (str), c));
    }
}

using CoinAmountJsonTests = testing::Test;

TEST_F (CoinAmountJsonTests, Valid)
{
  Amount a;

  ASSERT_TRUE (CoinAmountFromJson (ParseJson ("0"), a));
  EXPECT_EQ (a, 0);

  ASSERT_TRUE (CoinAmountFromJson (ParseJson ("42"), a));
  EXPECT_EQ (a, 42);

  ASSERT_TRUE (CoinAmountFromJson (ParseJson ("100000000000"), a));
  EXPECT_EQ (a, 100'000'000'000);
}

TEST_F (CoinAmountJsonTests, OutOfRange)
{
  Amount a;
  EXPECT_FALSE (CoinAmountFromJson (ParseJson ("-1"), a));
  EXPECT_FALSE (CoinAmountFromJson (ParseJson ("-50"), a));
  EXPECT_FALSE (CoinAmountFromJson (ParseJson ("100000000001"), a));
}

TEST_F (CoinAmountJsonTests, InvalidType)
{
  Amount a;
  EXPECT_FALSE (CoinAmountFromJson (ParseJson ("null"), a));
  EXPECT_FALSE (CoinAmountFromJson (ParseJson ("\"42\""), a));
  EXPECT_FALSE (CoinAmountFromJson (ParseJson ("1.5"), a));
  EXPECT_FALSE (CoinAmountFromJson (ParseJson ("10.0"), a));
  EXPECT_FALSE (CoinAmountFromJson (ParseJson ("1e2"), a));
}

using QuantityJsonTests = testing::Test;

TEST_F (QuantityJsonTests, Valid)
{
  Quantity q;

  ASSERT_TRUE (QuantityFromJson (ParseJson ("1"), q));
  EXPECT_EQ (q, 1);

  ASSERT_TRUE (QuantityFromJson (ParseJson ("42"), q));
  EXPECT_EQ (q, 42);

  ASSERT_TRUE (QuantityFromJson (ParseJson ("1125899906842624"), q));
  EXPECT_EQ (q, 1125899906842624);
}

TEST_F (QuantityJsonTests, OutOfRange)
{
  Quantity q;
  EXPECT_FALSE (QuantityFromJson (ParseJson ("0"), q));
  EXPECT_FALSE (QuantityFromJson (ParseJson ("-5"), q));
  EXPECT_FALSE (QuantityFromJson (ParseJson ("1125899906842625"), q));
}

TEST_F (QuantityJsonTests, InvalidType)
{
  Quantity q;
  EXPECT_FALSE (QuantityFromJson (ParseJson ("null"), q));
  EXPECT_FALSE (QuantityFromJson (ParseJson ("true"), q));
  EXPECT_FALSE (QuantityFromJson (ParseJson ("\"42\""), q));
  EXPECT_FALSE (QuantityFromJson (ParseJson ("1.5"), q));
  EXPECT_FALSE (QuantityFromJson (ParseJson ("10.0"), q));
  EXPECT_FALSE (QuantityFromJson (ParseJson ("1e2"), q));
}

using IdFromJsonTests = testing::Test;

TEST_F (IdFromJsonTests, Valid)
{
  Database::IdT id;

  ASSERT_TRUE (IdFromJson (ParseJson ("1"), id));
  EXPECT_EQ (id, 1);

  ASSERT_TRUE (IdFromJson (ParseJson ("42"), id));
  EXPECT_EQ (id, 42);

  ASSERT_TRUE (IdFromJson (ParseJson ("999999998"), id));
  EXPECT_EQ (id, 999999998);
}

TEST_F (IdFromJsonTests, Invalid)
{
  for (const std::string str : {"{}", "null",
                                "0", "999999999",
                                "-10", "1.5", "42.0", "2e2"})
    {
      Database::IdT id;
      EXPECT_FALSE (IdFromJson (ParseJson (str), id)) << str;
    }
}

using IntToJsonTests = testing::Test;

TEST_F (IntToJsonTests, UInt)
{
  const Json::Value res = IntToJson (std::numeric_limits<uint32_t>::max ());
  ASSERT_TRUE (res.isUInt ());
  EXPECT_FALSE (res.isInt ());
  EXPECT_EQ (res.asUInt (), std::numeric_limits<uint32_t>::max ());
}

TEST_F (IntToJsonTests, Int)
{
  const Json::Value res = IntToJson (std::numeric_limits<int32_t>::min ());
  ASSERT_TRUE (res.isInt ());
  EXPECT_FALSE (res.isUInt ());
  EXPECT_EQ (res.asInt (), std::numeric_limits<int32_t>::min ());
}

TEST_F (IntToJsonTests, UInt64)
{
  const Json::Value res = IntToJson (std::numeric_limits<uint64_t>::max ());
  ASSERT_TRUE (res.isUInt64 ());
  EXPECT_FALSE (res.isInt64 ());
  EXPECT_FALSE (res.isUInt ());
  EXPECT_EQ (res.asUInt64 (), std::numeric_limits<uint64_t>::max ());
}

TEST_F (IntToJsonTests, Int64)
{
  const Json::Value res = IntToJson (std::numeric_limits<int64_t>::min ());
  ASSERT_TRUE (res.isInt64 ());
  EXPECT_FALSE (res.isUInt64 ());
  EXPECT_FALSE (res.isInt ());
  EXPECT_EQ (res.asInt64 (), std::numeric_limits<int64_t>::min ());
}


} // anonymous namespace
} // namespace pxd
