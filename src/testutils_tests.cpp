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

#include "testutils.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace pxd
{
namespace
{

class PartialJsonEqualTests : public testing::Test
{

protected:

  bool
  PartialStrEqual (const std::string& actualStr, const std::string& expectedStr)
  {
    Json::Value actual;
    std::istringstream in1(actualStr);
    in1 >> actual;

    Json::Value expected;
    std::istringstream in2(expectedStr);
    in2 >> expected;

    return PartialJsonEqual (actual, expected);
  }

};

TEST_F (PartialJsonEqualTests, BasicValues)
{
  EXPECT_TRUE (PartialStrEqual ("42", "42"));
  EXPECT_TRUE (PartialStrEqual ("true", "true"));
  EXPECT_TRUE (PartialStrEqual ("-5.5", "-5.5"));
  EXPECT_TRUE (PartialStrEqual ("\"foo\"", " \"foo\""));

  EXPECT_FALSE (PartialStrEqual ("42", "0"));
  EXPECT_FALSE (PartialStrEqual ("1", "1.1"));
  EXPECT_FALSE (PartialStrEqual ("\"a\"", "\"b\""));
  EXPECT_FALSE (PartialStrEqual ("true", "false"));
}

TEST_F (PartialJsonEqualTests, Objects)
{
  EXPECT_FALSE (PartialStrEqual ("{}", "5"));
  EXPECT_FALSE (PartialStrEqual ("5", "{}"));

  EXPECT_FALSE (PartialStrEqual ("{}", R"({"foo": 42})"));
  EXPECT_TRUE (PartialStrEqual (R"({"foo": 42}")", "{}"));

  EXPECT_TRUE (PartialStrEqual (R"(
    {"foo": 5, "bar": 42, "baz": "abc"}
  )", R"(
    {"bar": 42, "baz": "abc", "test": null}
  )"));

  EXPECT_FALSE (PartialStrEqual (R"(
    {"foo": 5}
  )", R"(
    {"foo": null}
  )"));
  EXPECT_FALSE (PartialStrEqual (R"(
    {"foo": 5}
  )", R"(
    {"foo": 42}
  )"));
}

TEST_F (PartialJsonEqualTests, Arrays)
{
  EXPECT_FALSE (PartialStrEqual ("[]", "5"));
  EXPECT_FALSE (PartialStrEqual ("5", "[]"));

  EXPECT_FALSE (PartialStrEqual ("[]", "[5]"));
  EXPECT_FALSE (PartialStrEqual ("[5]", "[]"));
  EXPECT_FALSE (PartialStrEqual ("[5]", "[true]"));

  EXPECT_TRUE (PartialStrEqual ("[]", "[]"));
  EXPECT_TRUE (PartialStrEqual ("[5, -2.5, false]", "[5, -2.5, false]"));
}

TEST_F (PartialJsonEqualTests, Nested)
{
  EXPECT_TRUE (PartialStrEqual (R"(
    {
      "foo": [
        {"abc": 5, "def": 3},
        {}
      ],
      "bar": {
        "test": [42]
      }
    }
  )", R"(
    {
      "foo": [
        {"abc": 5},
        {}
      ],
      "bar": {
        "test": [42]
      }
    }
  )"));

  EXPECT_FALSE (PartialStrEqual (R"(
    {
      "foo": [
        {"abc": 5}
      ]
    }
  )", R"(
    {
      "foo": [
        {"abc": null}
      ]
    }
  )"));
}

} // anonymous namespace
} // namespace pxd
