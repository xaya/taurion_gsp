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

#include "pending.hpp"

#include "testutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace pxd
{
namespace
{

class PendingStateTests : public DBTestWithSchema
{

protected:

  PendingState state;

  CharacterTable characters;

  PendingStateTests ()
    : characters(db)
  {}

  /**
   * Expects that the current state matches the given one, after parsing
   * the expected state's string as JSON.  The comparison is done in the
   * "partial" sense.
   */
  void
  ExpectStateJson (const std::string& expectedStr)
  {
    Json::Value expected;
    std::istringstream in(expectedStr);
    in >> expected;

    const Json::Value actual = state.ToJson ();
    VLOG (1) << "Actual JSON for the pending state:\n" << actual;
    ASSERT_TRUE (PartialJsonEqual (actual, expected));
  }

};

TEST_F (PendingStateTests, Empty)
{
  ExpectStateJson (R"(
    {
      "characters": [],
      "newcharacters": {}
    }
  )");
}

TEST_F (PendingStateTests, Clear)
{
  state.AddCharacterCreation ("domob", Faction::RED);

  auto h = characters.CreateNew ("domob", Faction::GREEN);
  state.AddCharacterWaypoints (*h, {});
  h.reset ();

  ExpectStateJson (R"(
    {
      "characters": [{}],
      "newcharacters":
        {
          "domob": [{}]
        }
    }
  )");

  state.Clear ();
  ExpectStateJson (R"(
    {
      "characters": [],
      "newcharacters":
        {
          "domob": null
        }
    }
  )");
}

TEST_F (PendingStateTests, Waypoints)
{
  auto c1 = characters.CreateNew ("domob", Faction::RED);
  auto c2 = characters.CreateNew ("domob", Faction::GREEN);
  auto c3 = characters.CreateNew ("domob", Faction::BLUE);

  ASSERT_EQ (c1->GetId (), 1);
  ASSERT_EQ (c2->GetId (), 2);
  ASSERT_EQ (c3->GetId (), 3);

  state.AddCharacterWaypoints (*c1, {HexCoord (42, 5), HexCoord (0, 1)});
  state.AddCharacterWaypoints (*c2, {HexCoord (100, 3)});
  state.AddCharacterWaypoints (*c1, {HexCoord (2, 0), HexCoord (50, -49)});
  state.AddCharacterWaypoints (*c3, {});

  c1.reset ();
  c2.reset ();
  c3.reset ();

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "waypoints": [{"x": 2, "y": 0}, {"x": 50, "y": -49}]
          },
          {
            "id": 2,
            "waypoints": [{"x": 100, "y": 3}]
          },
          {
            "id": 3,
            "waypoints": []
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, CharacterCreation)
{
  state.AddCharacterCreation ("foo", Faction::RED);
  state.AddCharacterCreation ("bar", Faction::GREEN);
  state.AddCharacterCreation ("foo", Faction::RED);
  state.AddCharacterCreation ("bar", Faction::BLUE);

  ExpectStateJson (R"(
    {
      "newcharacters":
        {
          "foo":
            [
              {"faction": "r"},
              {"faction": "r"}
            ],
          "bar":
            [
              {"faction": "g"},
              {"faction": "b"}
            ]
        }
    }
  )");
}

} // anonymous namespace
} // namespace pxd
