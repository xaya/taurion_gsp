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
#include "database/itemcounts.hpp"
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

  ContextForTesting ctx;

  AccountsTable accounts;
  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;
  CharacterTable characters;
  ItemCounts itemCounts;
  OngoingsTable ongoings;
  RegionsTable regions;

  PendingStateTests ()
    : accounts(db),
      buildings(db), buildingInv(db),
      characters(db),
      itemCounts(db),
      ongoings(db),
      regions(db, 1'042)
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

TEST_F (PendingStateTests, FoundBuilding)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  state.AddCharacterDrop (*c);

  c = characters.CreateNew ("domob", Faction::RED);
  proto::ShapeTransformation trafo;
  trafo.set_rotation_steps (3);
  state.AddFoundBuilding (*c, "huesli", trafo);

  /* The second move will be ignored.  */
  state.AddFoundBuilding (*c, "checkmark", trafo);

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "foundbuilding": null
          },
          {
            "foundbuilding":
              {
                "type": "huesli",
                "rotationsteps": 3
              }
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, ChangeVehicle)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  state.AddCharacterDrop (*c);

  c = characters.CreateNew ("domob", Faction::RED);
  state.AddCharacterVehicle (*c, "chariot");
  state.AddCharacterVehicle (*c, "rv st");

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "changevehicle": null
          },
          {
            "changevehicle": "rv st"
          }
        ]
    }
  )");
}

TEST_F (PendingStateTests, Fitments)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  state.AddCharacterDrop (*c);

  c = characters.CreateNew ("domob", Faction::RED);
  state.AddCharacterFitments (*c, {});

  c = characters.CreateNew ("domob", Faction::RED);
  state.AddCharacterFitments (*c, {"sword"});
  state.AddCharacterFitments (*c, {"bow", "expander"});

  c.reset ();
  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "fitments": null
          },
          {
            "fitments": []
          },
          {
            "fitments": ["bow", "expander"]
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

TEST_F (PendingStateTests, ServiceOperations)
{
  accounts.CreateNew ("domob", Faction::RED)->AddBalance (30);
  accounts.CreateNew ("andy", Faction::GREEN)->AddBalance (30);

  db.SetNextId (100);
  buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
  buildings.CreateNew ("ancient2", "", Faction::ANCIENT);
  buildings.CreateNew ("ancient3", "", Faction::ANCIENT);

  buildingInv.Get (100, "domob")->GetInventory ()
      .AddFungibleCount ("test ore", 10);
  buildingInv.Get (101, "andy")->GetInventory ()
      .AddFungibleCount ("test ore", 10);
  buildingInv.Get (102, "domob")->GetInventory ()
      .AddFungibleCount ("test ore", 10);

  state.AddServiceOperation (*ServiceOperation::Parse (
      *accounts.GetByName ("domob"),
      ParseJson (R"({
        "b": 100,
        "t": "ref",
        "i": "test ore",
        "n": 3
      })"), ctx, accounts, buildings, buildingInv, characters,
            itemCounts, ongoings));
  state.AddServiceOperation (*ServiceOperation::Parse (
      *accounts.GetByName ("andy"),
      ParseJson (R"({
        "b": 101,
        "t": "ref",
        "i": "test ore",
        "n": 6
      })"), ctx, accounts, buildings, buildingInv, characters,
            itemCounts, ongoings));
  state.AddServiceOperation (*ServiceOperation::Parse (
      *accounts.GetByName ("domob"),
      ParseJson (R"({
        "b": 102,
        "t": "ref",
        "i": "test ore",
        "n": 9
      })"), ctx, accounts, buildings, buildingInv, characters,
            itemCounts, ongoings));

  ExpectStateJson (R"(
    {
      "accounts":
        [
          {
            "name": "andy",
            "serviceops": [{"building": 101}]
          },
          {
            "name": "domob",
            "serviceops": [{"building": 100}, {"building": 102}]
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

    DynObstacles dyn(db);
    PendingStateUpdater updater(db, dyn, state, ctx);
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
    characters.CreateNew ("at limit", Faction::BLUE)
        ->SetPosition (HexCoord (i, 1));

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

  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 1));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 2));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 3));

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

  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, -1));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 0));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 1));

  db.SetNextId (100);
  buildings.CreateNew ("huesli", "domob", Faction::RED)
      ->SetCentre (HexCoord (-1, 0));
  buildings.CreateNew ("huesli", "domob", Faction::RED)
      ->SetCentre (HexCoord (1, 0));

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
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 0));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (1, 0));

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

TEST_F (PendingStateUpdaterTests, PickupInFoundation)
{
  accounts.CreateNew ("domob", Faction::RED);
  auto b = buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  const auto bId = b->GetId ();
  b->MutableProto ().set_foundation (true);
  Inventory (*b->MutableProto ().mutable_construction_inventory ())
      .AddFungibleCount ("foo", 10);
  b.reset ();

  db.SetNextId (101);
  characters.CreateNew ("domob", Faction::RED)->SetBuildingId (bId);

  Process ("domob", R"({
    "c": {"101": {"pu": {"f": {"foo": 100}}}}
  })");
  ExpectStateJson (R"(
    {
      "characters": []
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
  h->MutableProto ().set_ongoing (42);
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

TEST_F (PendingStateUpdaterTests, FoundBuilding)
{
  accounts.CreateNew ("domob", Faction::RED);

  auto c = characters.CreateNew ("domob", Faction::RED);
  c->GetInventory ().AddFungibleCount ("foo", 10);
  c->SetPosition (HexCoord (10, 0));
  c.reset ();

  c = characters.CreateNew ("domob", Faction::RED);
  c->GetInventory ().AddFungibleCount ("foo", 10);
  c->SetPosition (HexCoord (-10, 0));
  c.reset ();

  /* This character will prevent the second "domob" from placing a building,
     verifying that the verification and DynObstacles map is correctly used
     for pending moves as well.  */
  c = characters.CreateNew ("andy", Faction::GREEN);
  c->SetPosition (HexCoord (-10, 0));
  c.reset ();

  Process ("domob", R"({
    "c":
      {
        "1": {"fb": {"t": "huesli", "rot": 3}},
        "2": {"fb": {"t": "huesli", "rot": 0}}
      }
  })");

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "foundbuilding":
              {
                "type": "huesli",
                "rotationsteps": 3
              }
          }
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, ChangeVehicle)
{
  accounts.CreateNew ("domob", Faction::RED);

  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->MutableProto ().set_vehicle ("rv st");

  auto b = buildings.CreateNew ("ancient1", "", Faction::RED);
  const auto bId = b->GetId ();
  c->SetBuildingId (bId);

  c.reset ();
  b.reset ();

  auto inv = buildingInv.Get (bId, "domob");
  inv->GetInventory ().AddFungibleCount ("chariot", 1);
  inv.reset ();

  /* Invalid move format.  */
  Process ("domob", R"({
    "c":
      {
        "1": {"v": ["chariot"]}
      }
  })");
  ExpectStateJson (R"(
    {
      "characters": []
    }
  )");

  /* This one is valid.  */
  Process ("domob", R"({
    "c":
      {
        "1": {"v": "chariot"}
      }
  })");
  ExpectStateJson (R"(
    {
      "characters":
        [
          {"changevehicle": "chariot"}
        ]
    }
  )");

  /* This one is invalid (item not owned) and thus does not change
     the pending state.  */
  Process ("domob", R"({
    "c":
      {
        "1": {"v": "rv st"}
      }
  })");
  ExpectStateJson (R"(
    {
      "characters":
        [
          {"changevehicle": "chariot"}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, Fitments)
{
  accounts.CreateNew ("domob", Faction::RED);

  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->MutableProto ().set_vehicle ("chariot");

  auto b = buildings.CreateNew ("ancient1", "", Faction::RED);
  const auto bId = b->GetId ();
  c->SetBuildingId (bId);

  c.reset ();
  b.reset ();

  auto inv = buildingInv.Get (bId, "domob");
  inv->GetInventory ().AddFungibleCount ("bow", 2);
  inv->GetInventory ().AddFungibleCount ("sword", 1);
  inv->GetInventory ().AddFungibleCount ("expander", 1);
  inv.reset ();

  /* Invalid move format, no fitments will be pending.  */
  Process ("domob", R"({
    "c":
      {
        "1": {"fit": [42]}
      }
  })");
  ExpectStateJson (R"(
    {
      "characters": []
    }
  )");

  /* This one is valid and sets pending fitments.  */
  Process ("domob", R"({
    "c":
      {
        "1": {"fit": ["sword", "expander"]}
      }
  })");
  ExpectStateJson (R"(
    {
      "characters":
        [
          {"fitments": ["sword", "expander"]}
        ]
    }
  )");

  /* This one is invalid (exceeding the complexity limit) and thus
     the previous one will remain.  */
  Process ("domob", R"({
    "c":
      {
        "1": {"fit": ["sword", "bow"]}
      }
  })");
  ExpectStateJson (R"(
    {
      "characters":
        [
          {"fitments": ["sword", "expander"]}
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

TEST_F (PendingStateUpdaterTests, ServiceOperations)
{
  accounts.CreateNew ("domob", Faction::RED)->AddBalance (100);

  db.SetNextId (100);
  buildings.CreateNew ("ancient1", "", Faction::ANCIENT)
      ->SetCentre  (HexCoord (100, 0));
  buildings.CreateNew ("ancient2", "", Faction::ANCIENT)
      ->SetCentre (HexCoord (-100, 0));

  buildingInv.Get (100, "domob")->GetInventory ()
      .AddFungibleCount ("test ore", 7);

  Process ("domob", R"({
    "s":
      [
        {"b": 100, "t": "ref", "i": "test ore", "n": 3},
        {"x": "invalid"},
        {"b": 101, "t": "ref", "i": "test ore", "n": 3},
        {"b": 100, "t": "ref", "i": "test ore", "n": 6}
      ]
  })");

  ExpectStateJson (R"(
    {
      "accounts":
        [
          {
            "name": "domob",
            "serviceops":
              [
                {"building": 100, "type": "refining", "input": {"test ore": 3}},
                {"building": 100, "type": "refining", "input": {"test ore": 6}}
              ]
          }
        ]
    }
  )");
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
