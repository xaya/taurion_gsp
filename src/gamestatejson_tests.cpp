#include "gamestatejson.hpp"

#include "protoutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/region.hpp"
#include "proto/character.pb.h"
#include "proto/region.pb.h"

#include <gtest/gtest.h>

#include <json/json.h>

#include <sstream>
#include <string>

namespace pxd
{
namespace
{

/* ************************************************************************** */

/**
 * Checks for "partial equality" of the given JSON values.  This means that
 * keys not present in the expected value (if it is an object) are not checked
 * in the actual value at all.  If keys have a value of null in expected,
 * then they must not be there in actual at all.
 */
bool
PartialJsonEqual (const Json::Value& actual, const Json::Value& expected)
{
  if (!expected.isObject () && !expected.isArray ())
    {
      /* Special case:  If both values are integers, then we compare them
         explicitly here.  This allows values of type "unsigned int" to be
         equal to values of type "int" (from golden data).  */
      if (actual.isInt64 () && expected.isInt64 ()
            && actual.asInt64 () == expected.asInt64 ())
        return true;

      if (actual == expected)
        return true;

      LOG (ERROR)
          << "Actual value:\n" << actual
          << "\nis not equal to expected:\n" << expected;
      return false;
    }

  if (expected.isArray ())
    {
      if (!actual.isArray ())
        {
          LOG (ERROR) << "Expected value is array, actual not: " << actual;
          return false;
        }

      if (actual.size () != expected.size ())
        {
          LOG (ERROR)
              << "Array sizes do not match: got " << actual.size ()
              << ", want " << expected.size ();
          return false;
        }

      for (unsigned i = 0; i < expected.size (); ++i)
        if (!PartialJsonEqual (actual[i], expected[i]))
          return false;

      return true;
    }

  if (!actual.isObject ())
    {
      LOG (ERROR) << "Expected value is object, actual not: " << actual;
      return false;
    }

  for (const auto& expectedKey : expected.getMemberNames ())
    {
      const auto& expectedVal = expected[expectedKey];
      if (expectedVal.isNull ())
        {
          if (actual.isMember (expectedKey))
            {
              LOG (ERROR)
                  << "Actual has member expected to be not there: "
                  << expectedKey;
              return false;
            }
          continue;
        }

      if (!actual.isMember (expectedKey))
        {
          LOG (ERROR)
              << "Actual does not have expected member: " << expectedKey;
          return false;
        }

      if (!PartialJsonEqual (actual[expectedKey], expected[expectedKey]))
        return false;
    }

  return true;
}

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

/* ************************************************************************** */

class GameStateJsonTests : public DBTestWithSchema
{

private:

  /** GameStateJson instance used in testing.  */
  GameStateJson converter;

protected:

  /** Basemap instance for the test.  */
  BaseMap map;

  GameStateJsonTests ()
    : converter(map)
  {}

  /**
   * Expects that the current state matches the given one, after parsing
   * the expected state's string as JSON.  Furthermore, the expected value
   * is assumed to be *partial* -- keys that are not present in the expected
   * value may be present with any value in the actual object.  If a key is
   * present in expected but has value null, then it must not be present
   * in the actual data, though.
   */
  void
  ExpectStateJson (const std::string& expectedStr)
  {
    Json::Value expected;
    std::istringstream in(expectedStr);
    in >> expected;

    const Json::Value actual = converter.FullState (db);
    VLOG (1) << "Actual JSON for the game state:\n" << actual;
    ASSERT_TRUE (PartialJsonEqual (actual, expected));
  }

};

/* ************************************************************************** */

class CharacterJsonTests : public GameStateJsonTests
{

protected:

  CharacterTable tbl;

  CharacterJsonTests ()
    : tbl(db)
  {}

};

TEST_F (CharacterJsonTests, Basic)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->SetPosition (HexCoord (-5, 2));
  c->MutableProto ().set_speed (750);
  c.reset ();

  tbl.CreateNew ("andy", Faction::GREEN);

  ExpectStateJson (R"({
    "characters":
      [
        {
          "id": 1, "owner": "domob", "faction": "r",
          "speed": 750,
          "position": {"x": -5, "y": 2}
        },
        {
          "id": 2, "owner": "andy", "faction": "g",
          "position": {"x": -0, "y": 0}
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, Waypoints)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->SetPartialStep (5);
  auto* wp = c->MutableProto ().mutable_movement ()->mutable_waypoints ();
  *wp->Add () = CoordToProto (HexCoord (-3, 0));
  *wp->Add () = CoordToProto (HexCoord (0, 42));
  c.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "movement":
            {
              "partialstep": 5,
              "waypoints": [{"x": -3, "y": 0}, {"x": 0, "y": 42}]
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, OnlyOneStep)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->SetPosition (HexCoord (2, 3));
  auto* mvProto = c->MutableProto ().mutable_movement ();
  *mvProto->mutable_waypoints ()->Add () = CoordToProto (HexCoord (42, -42));
  *mvProto->mutable_steps ()->Add () = CoordToProto (HexCoord (2, 3));
  c.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "movement":
            {
              "waypoints": [{"x": 42, "y": -42}],
              "steps": [{"x": 42, "y": -42}]
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, PositionIsLastStep)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->SetPosition (HexCoord (2, 3));
  auto* mvProto = c->MutableProto ().mutable_movement ();
  *mvProto->mutable_waypoints ()->Add () = CoordToProto (HexCoord (42, -42));
  *mvProto->mutable_steps ()->Add () = CoordToProto (HexCoord (-5, 10));
  *mvProto->mutable_steps ()->Add () = CoordToProto (HexCoord (2, 3));
  c.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "movement":
            {
              "waypoints": [{"x": 42, "y": -42}],
              "steps": [{"x": 42, "y": -42}]
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, MultipleStep)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->SetPosition (HexCoord (2, 3));
  auto* mvProto = c->MutableProto ().mutable_movement ();
  *mvProto->mutable_waypoints ()->Add () = CoordToProto (HexCoord (42, -42));
  *mvProto->mutable_steps ()->Add () = CoordToProto (HexCoord (-5, 10));
  *mvProto->mutable_steps ()->Add () = CoordToProto (HexCoord (2, 3));
  *mvProto->mutable_steps ()->Add () = CoordToProto (HexCoord (7, 8));
  c.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "movement":
            {
              "waypoints": [{"x": 42, "y": -42}],
              "steps":
                [
                  {"x": 7, "y": 8},
                  {"x": 42, "y": -42}
                ]
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, Target)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  auto* targetProto = c->MutableProto ().mutable_target ();
  targetProto->set_id (5);
  targetProto->set_type (proto::TargetId::TYPE_CHARACTER);
  c.reset ();

  c = tbl.CreateNew ("domob", Faction::GREEN);
  targetProto = c->MutableProto ().mutable_target ();
  targetProto->set_id (42);
  targetProto->set_type (proto::TargetId::TYPE_BUILDING);
  c.reset ();

  tbl.CreateNew ("domob", Faction::BLUE);

  ExpectStateJson (R"({
    "characters":
      [
        {"faction": "r", "combat": {"target": {"id": 5, "type": "character"}}},
        {"faction": "g", "combat": {"target": {"id": 42, "type": "building"}}},
        {"faction": "b", "combat": {"target": null}}
      ]
  })");
}

TEST_F (CharacterJsonTests, Attacks)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  auto* cd = c->MutableProto ().mutable_combat_data ();
  auto* attack = cd->add_attacks ();
  attack->set_range (5);
  attack->set_max_damage (10);
  attack = cd->add_attacks ();
  attack->set_range (1);
  attack->set_max_damage (1);
  c.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "combat":
            {
              "attacks":
                [
                  {"range": 5, "maxdamage": 10},
                  {"range": 1, "maxdamage": 1}
                ]
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, HP)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->MutableHP ().set_armour (42);
  c->MutableHP ().set_shield (5);
  c->MutableHP ().set_shield_mhp (1);
  auto* cd = c->MutableProto ().mutable_combat_data ();
  cd->mutable_max_hp ()->set_armour (100);
  cd->mutable_max_hp ()->set_shield (10);
  cd->set_shield_regeneration_mhp (1001);
  c.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "combat":
            {
              "hp":
                {
                  "max": {"armour": 100, "shield": 10},
                  "current": {"armour": 42, "shield": 5.001},
                  "regeneration": 1.001
                }
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, Prospecting)
{
  const HexCoord pos(10, -5);
  ASSERT_EQ (map.Regions ().GetRegionId (pos), 350146);

  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->SetPosition (HexCoord (10, -5));
  c->SetBusy (42);
  c->MutableProto ().mutable_prospection ();
  c.reset ();

  tbl.CreateNew ("notbusy", Faction::RED);

  ExpectStateJson (R"({
    "characters":
      [
        {
          "owner": "domob",
          "busy":
            {
              "blocks": 42,
              "operation": "prospecting",
              "region": 350146
            }
        },
        {
          "owner": "notbusy",
          "busy": null
        }
      ]
  })");
}

/* ************************************************************************** */

class RegionJsonTests : public GameStateJsonTests
{

protected:

  RegionsTable tbl;

  RegionJsonTests ()
    : tbl(db)
  {}

};

TEST_F (RegionJsonTests, Empty)
{
  /* This region is never changed to be non-trivial, and thus not in the result
     of the database query at all.  */
  tbl.GetById (10);

  /* This region ends up in a trivial state, but is written to the database
     because it is changed temporarily.  */
  tbl.GetById (20)->MutableProto ().set_prospecting_character (42);
  tbl.GetById (20)->MutableProto ().clear_prospecting_character ();


  ExpectStateJson (R"({
    "regions":
      [
        {"id": 20}
      ]
  })");
}

TEST_F (RegionJsonTests, Prospection)
{
  tbl.GetById (20)->MutableProto ().set_prospecting_character (42);
  tbl.GetById (10)->MutableProto ().mutable_prospection ()->set_name ("foo");

  ExpectStateJson (R"({
    "regions":
      [
        {"id": 10, "prospection": {"name": "foo"}},
        {"id": 20, "prospection": {"inprogress": 42}}
      ]
  })");
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
