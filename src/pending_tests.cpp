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

#include "pending.hpp"

#include "jsonutils.hpp"
#include "testutils.hpp"

#include "database/account.hpp"
#include "database/building.hpp"
#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/region.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class PendingStateTests : public DBTestWithSchema
{

protected:

  PendingState state;

  AccountsTable accounts;
  CharacterTable characters;
  RegionsTable regions;

  PendingStateTests ()
    : accounts(db), characters(db), regions(db, 1'042)
  {}

  /**
   * Expects that the current state matches the given one, after parsing
   * the expected state's string as JSON.  The comparison is done in the
   * "partial" sense.
   */
  void
  ExpectStateJson (const std::string& expectedStr)
  {
    const Json::Value actual = state.ToJson ();
    VLOG (1) << "Actual JSON for the pending state:\n" << actual;
    ASSERT_TRUE (PartialJsonEqual (actual, ParseJson (expectedStr)));
  }

};

TEST_F (PendingStateTests, Empty)
{
  ExpectStateJson (R"(
    {
      "characters": [],
      "newcharacters": [],
      "accounts": []
    }
  )");
}

TEST_F (PendingStateTests, Clear)
{
  auto a = accounts.CreateNew ("domob", Faction::RED);
  CoinTransferBurn coinOp;
  coinOp.burnt = 10;
  coinOp.transfers["andy"] = 20;
  state.AddCoinTransferBurn (*a, coinOp);
  a.reset ();

  state.AddCharacterCreation ("domob", Faction::RED);

  auto h = characters.CreateNew ("domob", Faction::RED);
  state.AddCharacterWaypoints (*h, {});
  state.AddEnterBuilding (*h, 42);
  state.AddCharacterDrop (*h);
  state.AddCharacterPickup (*h);
  h.reset ();

  ExpectStateJson (R"(
    {
      "characters": [{}],
      "newcharacters": [{}],
      "accounts": [{}]
    }
  )");

  state.Clear ();
  ExpectStateJson (R"(
    {
      "characters": [],
      "newcharacters": [],
      "accounts": []
    }
  )");
}

TEST_F (PendingStateTests, Waypoints)
{
  auto c1 = characters.CreateNew ("domob", Faction::RED);
  auto c2 = characters.CreateNew ("domob", Faction::RED);
  auto c3 = characters.CreateNew ("domob", Faction::RED);

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

TEST_F (PendingStateTests, EnterBuilding)
{
  auto c1 = characters.CreateNew ("domob", Faction::RED);
  auto c2 = characters.CreateNew ("domob", Faction::RED);

  ASSERT_EQ (c1->GetId (), 1);
  ASSERT_EQ (c2->GetId (), 2);

  state.AddEnterBuilding (*c1, 42);
  state.AddEnterBuilding (*c1, 45);
  state.AddEnterBuilding (*c2, Database::EMPTY_ID);

  c1.reset ();
  c2.reset ();

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "enterbuilding": 45
          },
          {
            "id": 2,
            "enterbuilding": "null"
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, ExitBuilding)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetBuildingId (42);
  state.AddExitBuilding (*c);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 2);
  state.AddCharacterMining (*c, 12345);
  c.reset ();

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "exitbuilding": {"building": 42}
          },
          {
            "id": 2,
            "exitbuilding": null
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, DropPickup)
{
  auto c1 = characters.CreateNew ("domob", Faction::RED);
  auto c2 = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c1->GetId (), 1);
  ASSERT_EQ (c2->GetId (), 2);

  state.AddCharacterDrop (*c1);
  state.AddCharacterPickup (*c2);

  c1.reset ();
  c2.reset ();

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "drop": true,
            "pickup": false
          },
          {
            "id": 2,
            "drop": false,
            "pickup": true
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, Prospecting)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);

  state.AddCharacterProspecting (*c, 12345);
  state.AddCharacterProspecting (*c, 12345);
  EXPECT_DEATH (state.AddCharacterProspecting (*c, 999), "another region");

  c.reset ();

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "prospecting": 12345
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, ProspectingAndWaypoints)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);

  state.AddCharacterProspecting (*c, 12345);
  state.AddCharacterWaypoints (*c, {});

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "prospecting": 12345,
            "waypoints": null
          }
        ]
    }
  )");

  c = characters.GetById (1);

  state.Clear ();
  state.AddCharacterWaypoints (*c, {});
  state.AddCharacterProspecting (*c, 12345);

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "prospecting": 12345,
            "waypoints": null
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, Mining)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);

  state.AddCharacterMining (*c, 12345);
  state.AddCharacterMining (*c, 12345);
  EXPECT_DEATH (state.AddCharacterMining (*c, 999), "another region");

  c.reset ();

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "mining": 12345
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, MiningNotPossible)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);

  state.AddCharacterWaypoints (*c, {});
  state.AddCharacterMining (*c, 12345);

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "mining": null
          }
        ]
    }
  )");

  c = characters.GetById (1);

  state.Clear ();
  state.AddCharacterProspecting (*c, 12345);
  state.AddCharacterMining (*c, 12345);

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "mining": null
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, MiningCancelledByWaypoints)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);

  state.AddCharacterMining (*c, 12345);

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "mining": 12345
          }
        ]
    }
  )");

  c = characters.GetById (1);
  state.AddCharacterWaypoints (*c, {});

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "mining": null
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
  state.AddCharacterCreation ("bar", Faction::GREEN);

  ExpectStateJson (R"(
    {
      "newcharacters":
        [
          {
            "name": "bar",
            "creations":
              [
                {"faction": "g"},
                {"faction": "g"}
              ]
          },
          {
            "name": "foo",
            "creations":
              [
                {"faction": "r"},
                {"faction": "r"}
              ]
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, CoinTransferBurn)
{
  auto a = accounts.CreateNew ("domob", Faction::RED);

  CoinTransferBurn coinOp;
  coinOp.burnt = 5;
  coinOp.transfers["andy"] = 20;
  state.AddCoinTransferBurn (*a, coinOp);

  coinOp.burnt = 2;
  coinOp.transfers["andy"] = 1;
  coinOp.transfers["daniel"] = 10;
  state.AddCoinTransferBurn (*a, coinOp);

  a.reset ();

  ExpectStateJson (R"(
    {
      "accounts":
        [
          {
            "name": "domob",
            "coinops":
              {
                "burnt": 7,
                "transfers":
                  {
                    "andy": 21,
                    "daniel": 10
                  }
              }
          }
        ]
    }
  )");
}

/* ************************************************************************** */

class PendingStateUpdaterTests : public PendingStateTests
{

protected:

  ContextForTesting ctx;

private:

  PendingStateUpdater updater;

protected:

  BuildingsTable buildings;

  PendingStateUpdaterTests ()
    : updater(db, state, ctx), buildings(db)
  {}

  /**
   * Processes a move for the given name and with the given move data, parsed
   * from JSON string.  If paidToDev is non-zero, then add an "out" entry
   * paying the given amount to the dev address.
   */
  void
  ProcessWithDevPayment (const std::string& name, const Amount paidToDev,
                         const std::string& mvStr)
  {
    Json::Value moveObj(Json::objectValue);
    moveObj["name"] = name;
    moveObj["move"] = ParseJson (mvStr);

    if (paidToDev != 0)
      moveObj["out"][ctx.Params ().DeveloperAddress ()]
          = AmountToJson (paidToDev);

    updater.ProcessMove (moveObj);
  }

  /**
   * Processes a move for the given name and with the given move data
   * as JSON string, without developer payment.
   */
  void
  Process (const std::string& name, const std::string& mvStr)
  {
    ProcessWithDevPayment (name, 0, mvStr);
  }

};

TEST_F (PendingStateUpdaterTests, AccountNotInitialised)
{
  ProcessWithDevPayment ("domob", ctx.Params ().CharacterCost (), R"({
    "nc": [{}]
  })");

  ExpectStateJson (R"(
    {
      "newcharacters": []
    }
  )");
}

TEST_F (PendingStateUpdaterTests, InvalidCreation)
{
  accounts.CreateNew ("domob", Faction::RED);

  accounts.CreateNew ("at limit", Faction::BLUE);
  for (unsigned i = 0; i < ctx.Params ().CharacterLimit (); ++i)
    characters.CreateNew ("at limit", Faction::BLUE);

  ProcessWithDevPayment ("domob", ctx.Params ().CharacterCost (), R"(
    {
      "nc": [{"faction": "r"}]
    }
  )");
  Process ("domob", R"(
    {
      "nc": [{}]
    }
  )");
  ProcessWithDevPayment ("at limit", ctx.Params ().CharacterCost (), R"(
    {
      "nc": [{}]
    }
  )");

  ExpectStateJson (R"(
    {
      "newcharacters": []
    }
  )");
}

TEST_F (PendingStateUpdaterTests, ValidCreations)
{
  accounts.CreateNew ("domob", Faction::RED);
  accounts.CreateNew ("andy", Faction::GREEN);

  ProcessWithDevPayment ("domob", 2 * ctx.Params ().CharacterCost (), R"({
    "nc": [{}, {}, {}]
  })");
  ProcessWithDevPayment ("andy", ctx.Params ().CharacterCost (), R"({
    "nc": [{}]
  })");

  ExpectStateJson (R"(
    {
      "newcharacters":
        [
          {
            "name": "andy",
            "creations":
              [
                {"faction": "g"}
              ]
          },
          {
            "name": "domob",
            "creations":
              [
                {"faction": "r"},
                {"faction": "r"}
              ]
          }
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, InvalidUpdate)
{
  accounts.CreateNew ("domob", Faction::RED);
  accounts.CreateNew ("andy", Faction::RED);

  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 1);

  Process ("andy", R"({
    "c": {"1": {"wp": []}}
  })");
  Process ("domob", R"({
    "c": {" 1 ": {"wp": []}}
  })");
  Process ("domob", R"({
    "c": {"42": {"wp": []}}
  })");

  ExpectStateJson (R"(
    {
      "characters": []
    }
  )");
}

TEST_F (PendingStateUpdaterTests, Waypoints)
{
  accounts.CreateNew ("domob", Faction::RED);

  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 1);
  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 2);
  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 3);

  /* Some invalid updates that will just not show up (i.e. ID 1 will have no
     pending updates later on).  */
  Process ("domob", R"({
    "c": {"1": {"wp": "foo"}}
  })");
  Process ("domob", R"({
    "c": {"1": {"wp": {"x": 4.5, "y": 3.141}}}
  })");

  /* Perform valid updates.  Only the waypoints updates will be tracked, and
     we will keep the latest one for any character.  */
  Process ("domob", R"({
    "c":
      {
        "2": {"wp": [{"x": 0, "y": 100}]},
        "3": {"wp": [{"x": 1, "y": -2}]}
      }
  })");
  Process ("domob", R"({
    "c": {"2": {"wp": []}}
  })");
  Process ("domob", R"({
    "c": {"2": {"send": "andy"}}
  })");

  ExpectStateJson (R"(
    {
      "characters":
        [
          {"id": 2, "waypoints": []},
          {"id": 3, "waypoints": [{"x": 1, "y": -2}]}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, EnterBuilding)
{
  accounts.CreateNew ("domob", Faction::RED);

  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 1);
  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 2);
  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 3);

  db.SetNextId (100);
  buildings.CreateNew ("checkmark", "domob", Faction::RED);
  buildings.CreateNew ("checkmark", "domob", Faction::RED);

  /* Some invalid updates that will just not show up (i.e. ID 1 will have no
     pending updates later on).  */
  Process ("domob", R"({
    "c": {"1": {"eb": "foo"}}
  })");
  Process ("domob", R"({
    "c": {"1": {"eb": 20}}
  })");

  /* Perform valid updates.  */
  Process ("domob", R"({
    "c":
      {
        "2": {"eb": 100},
        "3": {"eb": null}
      }
  })");
  Process ("domob", R"({
    "c":
      {
        "2": {"eb": 101}
      }
  })");

  ExpectStateJson (R"(
    {
      "characters":
        [
          {"id": 2, "enterbuilding": 101},
          {"id": 3, "enterbuilding": "null"}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, ExitBuilding)
{
  accounts.CreateNew ("domob", Faction::RED);

  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 2);
  c->SetBuildingId (20);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 3);
  c->SetBuildingId (20);
  c.reset ();

  /* Some invalid updates that will just not show up (i.e. IDs 1 and 2 will
     have no pending updates later on).  */
  Process ("domob", R"({
    "c": {"1": {"xb": {}}}
  })");
  Process ("domob", R"({
    "c": {"2": {"xb": 20}}
  })");

  /* Perform valid update.  */
  Process ("domob", R"({
    "c":
      {
        "3": {"xb": {}}
      }
  })");

  ExpectStateJson (R"(
    {
      "characters":
        [
          {"id": 3, "exitbuilding": {"building": 20}}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, DropPickup)
{
  accounts.CreateNew ("domob", Faction::RED);
  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 1);
  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 2);

  /* Some invalid / empty commands.  */
  Process ("domob", R"({
    "c": {"1": {"drop": []}}
  })");
  Process ("domob", R"({
    "c": {"2": {"pu": {"f": {}}}}
  })");
  Process ("domob", R"({
    "c": {"1": {"drop": {"f": {"foo": 0}}}}
  })");
  Process ("domob", R"({
    "c": {"1": {"drop": {"f": {"invalid item": 1}}}}
  })");
  Process ("domob", R"({
    "c": {"1": {"pu": {"f": {"invalid item": 1}}}}
  })");

  /* Valid drop/pickup commands (character 1 will pickup, character 2 will
     drop, but not the corresponding other command).  */
  Process ("domob", R"({
    "c": {"1": {"pu": {"f": {"foo": 1}}}}
  })");
  Process ("domob", R"({
    "c": {"2": {"drop": {"f": {"bar": 10}}}}
  })");

  ExpectStateJson (R"(
    {
      "characters":
        [
          {"id": 1, "drop": false, "pickup": true},
          {"id": 2, "drop": true, "pickup": false}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, Prospecting)
{
  accounts.CreateNew ("domob", Faction::RED);

  const HexCoord pos(456, -789);
  auto h = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (h->GetId (), 1);
  h->SetPosition (pos);
  h.reset ();

  h = characters.CreateNew ("domob", Faction::GREEN);
  ASSERT_EQ (h->GetId (), 2);
  h->SetBusy (10);
  h->MutableProto ().mutable_prospection ();
  h.reset ();

  Process ("domob", R"({
    "c":
      {
        "1": {"prospect": {}},
        "2": {"prospect": {}}
      }
  })");

  ExpectStateJson (R"(
    {
      "characters":
        [
          {"id": 1, "prospecting": 345820}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, Mining)
{
  accounts.CreateNew ("domob", Faction::RED);

  const HexCoord pos(456, -789);
  constexpr Database::IdT regionId = 345'820;
  auto r = regions.GetById (regionId);
  r->MutableProto ().mutable_prospection ();
  r->SetResourceLeft (100);
  r.reset ();

  auto h = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (h->GetId (), 1);
  h->SetPosition (pos);
  h->MutableProto ().mutable_mining ();
  h.reset ();

  h = characters.CreateNew ("domob", Faction::GREEN);
  ASSERT_EQ (h->GetId (), 2);
  h.reset ();

  Process ("domob", R"({
    "c":
      {
        "1": {"mine": {}},
        "2": {"mine": {}}
      }
  })");

  ExpectStateJson (R"(
    {
      "characters":
        [
          {"id": 1, "mining": 345820}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, CreationAndUpdateTogether)
{
  accounts.CreateNew ("domob", Faction::RED);

  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 1);

  ProcessWithDevPayment ("domob", ctx.Params ().CharacterCost (), R"({
    "nc": [{}],
    "c": {"1": {"wp": []}}
  })");

  ExpectStateJson (R"(
    {
      "characters": [{"id": 1, "waypoints": []}],
      "newcharacters":
        [
          {
            "name": "domob",
            "creations": [{"faction": "r"}]
          }
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, CoinTransferBurn)
{
  accounts.CreateNew ("domob", Faction::RED)->AddBalance (100);
  accounts.CreateNew ("andy", Faction::GREEN)->AddBalance (100);

  Process ("domob", R"({
    "abc": "foo",
    "vc": {"x": 5, "b": 10}
  })");
  Process ("andy", R"({
    "vc": {"b": -10, "t": {"domob": 5, "invalid": 2}}
  })");
  Process ("invalid", R"({
    "vc": {"b": 1}
  })");
  Process ("domob", R"({
    "abc": "foo",
    "vc": {"b": 2, "t": {"domob": 1, "andy": 5}}
  })");
  Process ("andy", R"({
    "vc": {"b": 101, "t": {"domob": 101}}
  })");

  ExpectStateJson (R"(
    {
      "accounts":
        [
          {
            "name": "andy",
            "coinops":
              {
                "burnt": 0,
                "transfers": {"domob": 5}
              }
          },
          {
            "name": "domob",
            "coinops":
              {
                "burnt": 12,
                "transfers": {"andy": 5}
              }
          }
        ]
    }
  )");
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
