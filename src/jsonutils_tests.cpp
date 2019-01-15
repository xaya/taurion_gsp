#include "jsonutils.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <sstream>

namespace pxd
{
namespace
{

const Json::Value
ParseJson (const std::string& str)
{
  Json::Value val;
  std::istringstream in(str);
  in >> val;
  return val;
}

using JsonCoordTests = testing::Test;

TEST_F (JsonCoordTests, CoordToJson)
{
  const Json::Value val = CoordToJson (HexCoord (-5, 42));
  ASSERT_TRUE (val.isObject ());
  EXPECT_EQ (val.size (), 2);
  ASSERT_TRUE (val.isMember ("x"));
  ASSERT_TRUE (val.isMember ("y"));
  ASSERT_TRUE (val["x"].isInt ());
  ASSERT_TRUE (val["y"].isInt ());
  EXPECT_EQ (val["x"].asInt (), -5);
  EXPECT_EQ (val["y"].asInt (), 42);
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
                          R"({"x": -1, "y": 1000000000})",
                          R"({"x": 0, "y": 0, "foo": 0})"})
    {
      HexCoord c;
      EXPECT_FALSE (CoordFromJson (ParseJson (str), c));
    }
}

using JsonAmountTests = testing::Test;

TEST_F (JsonAmountTests, AmountToJson)
{
  const Json::Value val = AmountToJson (COIN);
  ASSERT_TRUE (val.isDouble ());
  ASSERT_EQ (val.asDouble (), 1.0);
}

TEST_F (JsonAmountTests, ValidAmountRoundtrip)
{
  const Amount testValues[] = {
      0, 1,
      COIN - 1, COIN, COIN + 1,
      MAX_AMOUNT - 1, MAX_AMOUNT
  };
  for (const Amount a : testValues)
    {
      LOG (INFO) << "Testing with amount " << a;
      const Json::Value val = AmountToJson (a);
      Amount a2;
      ASSERT_TRUE (AmountFromJson (val, a2));
      EXPECT_EQ (a2, a);
    }
}

TEST_F (JsonAmountTests, InvalidAmountFromJson)
{
  for (const auto& str : {"{}", "\"foo\"", "true", "-0.1", "80000000.1"})
    {
      Amount a;
      EXPECT_FALSE (AmountFromJson (ParseJson (str), a));
    }
}

using IdFromStringTests = testing::Test;

TEST_F (IdFromStringTests, Valid)
{
  unsigned id;

  ASSERT_TRUE (IdFromString ("1", id));
  EXPECT_EQ (id, 1);

  ASSERT_TRUE (IdFromString ("999", id));
  EXPECT_EQ (id, 999);

  ASSERT_TRUE (IdFromString ("4000000000", id));
  EXPECT_EQ (id, 4000000000);
}

TEST_F (IdFromStringTests, Invalid)
{
  for (const std::string str : {"0", "-5", "2.3", " 5", "42 ", "02"})
    {
      unsigned id;
      EXPECT_FALSE (IdFromString (str, id));
    }
}

} // anonymous namespace
} // namespace pxd
