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
#include "protoutils.hpp"
#include "testutils.hpp"

#include "database/account.hpp"
#include "database/building.hpp"
#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/dex.hpp"
#include "database/itemcounts.hpp"
#include "database/region.hpp"

#include <xayautil/jsonutils.hpp>

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
  DexOrderTable orders;
  DexHistoryTable history;
  ItemCounts itemCounts;
  OngoingsTable ongoings;
  RegionsTable regions;

  PendingStateTests ()
    : accounts(db),
      buildings(db), buildingInv(db),
      characters(db),
      orders(db), history(db),
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
  auto a = accounts.CreateNew ("domob");
  a->SetFaction (Faction::RED);
  CoinTransferBurn coinOp;
  coinOp.minted = 5;
  coinOp.burnt = 10;
  coinOp.transfers["andy"] = 20;
  state.AddCoinTransferBurn (*a, coinOp);
  a.reset ();

  state.AddCharacterCreation ("domob", Faction::RED);

  auto h = characters.CreateNew ("domob", Faction::RED);
  state.AddCharacterWaypoints (*h, {}, true);
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
  auto* wp = c3->MutableProto ().mutable_movement ()->mutable_waypoints ();
  AddRepeatedCoords ({HexCoord (-42, 30)}, *wp);

  ASSERT_EQ (c1->GetId (), 1);
  ASSERT_EQ (c2->GetId (), 2);
  ASSERT_EQ (c3->GetId (), 3);

  state.AddCharacterWaypoints (*c1, {HexCoord (42, 5), HexCoord (0, 1)}, true);
  state.AddCharacterWaypoints (*c2, {HexCoord (100, 3)}, true);
  state.AddCharacterWaypoints (*c1, {HexCoord (2, 0), HexCoord (5, -5)}, true);
  state.AddCharacterWaypoints (*c3, {}, true);

  c1.reset ();
  c2.reset ();
  c3.reset ();

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "waypoints": [{"x": 2, "y": 0}, {"x": 5, "y": -5}]
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

TEST_F (PendingStateTests, WaypointExtension)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  auto* wp = c->MutableProto ().mutable_movement ()->mutable_waypoints ();
  AddRepeatedCoords ({HexCoord (-42, 30)}, *wp);

  state.AddCharacterWaypoints (*c, {HexCoord (1, 2)}, false);
  state.AddCharacterWaypoints (*c, {HexCoord (-5, 10)}, false);

  c.reset ();

  ExpectStateJson (R"(
    {
      "characters":
        [
          {
            "id": 1,
            "waypoints":
              [
                {"x": -42, "y": 30},
                {"x": 1, "y": 2},
                {"x": -5, "y": 10}
              ]
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
  state.AddCharacterWaypoints (*c, {}, true);

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
  state.AddCharacterWaypoints (*c, {}, true);
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

  state.AddCharacterWaypoints (*c, {}, true);
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
  state.AddCharacterWaypoints (*c, {}, true);

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
  state.AddCharacterFitments (*c, {"bow", "super scanner"});

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
            "fitments": ["bow", "super scanner"]
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
  auto a = accounts.CreateNew ("domob");

  CoinTransferBurn coinOp;
  coinOp.minted = 32;
  coinOp.burnt = 5;
  coinOp.transfers["andy"] = 20;
  state.AddCoinTransferBurn (*a, coinOp);

  coinOp.minted = 10;
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
                "minted": 42,
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
  auto a = accounts.CreateNew ("domob");
  a->SetFaction (Faction::RED);
  a->AddBalance (30);
  a.reset ();

  a = accounts.CreateNew ("andy");
  a->SetFaction (Faction::GREEN);
  a->AddBalance (30);
  a.reset ();

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

TEST_F (PendingStateTests, DexOperations)
{
  auto domob = accounts.CreateNew ("domob");
  auto andy = accounts.CreateNew ("andy");

  db.SetNextId (100);
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  buildingInv.Get (100, "domob")->GetInventory ()
      .AddFungibleCount ("foo", 100);
  buildingInv.Get (100, "andy")->GetInventory ()
      .AddFungibleCount ("bar", 100);

  state.AddDexOperation (*DexOperation::Parse (
      *domob,
      ParseJson (R"({
        "b": 100,
        "i": "foo",
        "n": 1,
        "t": "daniel"
      })"), ctx, accounts, buildings, buildingInv, orders, history));
  state.AddDexOperation (*DexOperation::Parse (
      *andy,
      ParseJson (R"({
        "b": 100,
        "i": "bar",
        "n": 2,
        "t": "daniel"
      })"), ctx, accounts, buildings, buildingInv, orders, history));
  state.AddDexOperation (*DexOperation::Parse (
      *domob,
      ParseJson (R"({
        "b": 100,
        "i": "foo",
        "n": 3,
        "t": "daniel"
      })"), ctx, accounts, buildings, buildingInv, orders, history));

  ExpectStateJson (R"(
    {
      "accounts":
        [
          {
            "name": "andy",
            "dexops": [{"num": 2}]
          },
          {
            "name": "domob",
            "dexops": [{"num": 1}, {"num": 3}]
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

  /** Cost of one character.  */
  const Amount characterCost;

  PendingStateUpdaterTests ()
    : characterCost(ctx.RoConfig ()->params ().character_cost () * COIN)
  {}

  /**
   * Processes a move for the given name and with the given move data, parsed
   * from JSON string.  If paidToDev is non-zero, then add an "out" entry
   * paying the given amount to the dev address.  If burntChi is non-zero,
   * then also a CHI burn is added to the JSON data.
   */
  void
  ProcessWithDevPaymentAndBurn (const std::string& name,
                                const Amount paidToDev, const Amount burntChi,
                                const std::string& mvStr)
  {
    Json::Value moveObj(Json::objectValue);
    moveObj["name"] = name;
    moveObj["move"] = ParseJson (mvStr);

    if (paidToDev != 0)
      moveObj["out"][ctx.RoConfig ()->params ().dev_addr ()]
          = xaya::ChiAmountToJson (paidToDev);
    if (burntChi != 0)
      moveObj["burnt"] = xaya::ChiAmountToJson (burntChi);

    DynObstacles dyn(db, ctx);
    PendingStateUpdater updater(db, dyn, state, ctx);
    updater.ProcessMove (moveObj);
  }

  /**
   * Processes a move for the given name, data and dev payment, without burn.
   */
  void
  ProcessWithDevPayment (const std::string& name, const Amount paidToDev,
                         const std::string& mvStr)
  {
    ProcessWithDevPaymentAndBurn (name, paidToDev, 0, mvStr);
  }

  /**
   * Processes a move for the given name, data and burn.
   */
  void
  ProcessWithBurn (const std::string& name, const Amount burntChi,
                   const std::string& mvStr)
  {
    ProcessWithDevPaymentAndBurn (name, 0, burntChi, mvStr);
  }

  /**
   * Processes a move for the given name and with the given move data
   * as JSON string, without developer payment or burn.
   */
  void
  Process (const std::string& name, const std::string& mvStr)
  {
    ProcessWithDevPaymentAndBurn (name, 0, 0, mvStr);
  }

};

TEST_F (PendingStateUpdaterTests, AccountNotInitialised)
{
  ProcessWithDevPayment ("domob", characterCost, R"({
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  accounts.CreateNew ("at limit")->SetFaction (Faction::BLUE);
  for (unsigned i = 0; i < ctx.RoConfig ()->params ().character_limit (); ++i)
    characters.CreateNew ("at limit", Faction::BLUE)
        ->SetPosition (HexCoord (i, 1));

  ProcessWithDevPayment ("domob", characterCost, R"(
    {
      "nc": [{"faction": "r"}]
    }
  )");
  Process ("domob", R"(
    {
      "nc": [{}]
    }
  )");
  ProcessWithDevPayment ("at limit", characterCost, R"(
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  accounts.CreateNew ("andy")->SetFaction (Faction::GREEN);

  ProcessWithDevPayment ("domob", 2 * characterCost, R"({
    "nc": [{}, {}, {}]
  })");
  ProcessWithDevPayment ("andy", characterCost, R"({
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  accounts.CreateNew ("andy")->SetFaction (Faction::RED);

  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 1);

  Process ("andy", R"({
    "c": {"id": 1, "wp": null}
  })");
  Process ("domob", R"({
    "c": {"wp": null}
  })");
  Process ("domob", R"({
    "c": {"id": 42, "wp": null}
  })");

  ExpectStateJson (R"(
    {
      "characters": []
    }
  )");
}

TEST_F (PendingStateUpdaterTests, Waypoints)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 1));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 2));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 3));

  /* An invalid update that will just not show up (i.e. ID 1 will have no
     pending updates later on).  */
  Process ("domob", R"({
    "c": {"id": 1, "wp": "foo"}
  })");

  /* Perform valid updates.  Only the waypoints updates will be tracked, and
     we will keep the latest one for any character.  */
  Process ("domob", R"({
    "c":
      [
        {"id": 2, "wp": )" + WpStr ({HexCoord (0, 100)}) + R"(},
        {"id": 3, "wp": )" + WpStr ({HexCoord (1, -2)}) + R"(}
      ]
  })");
  Process ("domob", R"({
    "c": {"id": 2, "wp": null}
  })");
  Process ("domob", R"({
    "c": {"id": 2, "send": "andy"}
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

TEST_F (PendingStateUpdaterTests, WaypointExtension)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 1));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 2));

  auto c = characters.CreateNew ("domob", Faction::RED);
  c->SetPosition (HexCoord (0, 3));
  auto* wp = c->MutableProto ().mutable_movement ()->mutable_waypoints ();
  AddRepeatedCoords ({HexCoord (0, 30)}, *wp);
  c.reset ();

  /* Character 1 isn't moving, so the extension is not valid.  The other
     two will be extended, because they already have existing confirmed
     waypoints or get pending ones added before.  */
  Process ("domob", R"({
    "c":
      [
        {
          "id": 2,
          "wp": )" + WpStr ({HexCoord (-5, 2)}) + R"(,
          "wpx": )" + WpStr ({HexCoord (-6, 3)}) + R"(
        },
        {
          "id": 3,
          "wpx": )" + WpStr ({HexCoord (0, 0)}) + R"(
        }
      ]
  })");

  ExpectStateJson (R"(
    {
      "characters":
        [
          {"id": 2, "waypoints": [{"x": -5, "y": 2}, {"x": -6, "y": 3}]},
          {"id": 3, "waypoints": [{"x": 0, "y": 30}, {"x": 0, "y": 0}]}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, EnterBuilding)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

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
    "c": {"id": 1, "eb": "foo"}
  })");
  Process ("domob", R"({
    "c": {"id": 1, "eb": 20}
  })");

  /* Perform valid updates.  */
  Process ("domob", R"({
    "c":
      [
        {"id": 2, "eb": 100},
        {"id": 3, "eb": null}
      ]
  })");
  Process ("domob", R"({
    "c": {"id": 2, "eb": 101}
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

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
    "c": {"id": 1, "xb": {}}
  })");
  Process ("domob", R"({
    "c": {"id": 2, "xb": 20}
  })");

  /* Perform valid update.  */
  Process ("domob", R"({
    "c": {"id": 3, "xb": {}}
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (0, 0));
  characters.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (1, 0));

  /* Some invalid / empty commands.  */
  Process ("domob", R"({
    "c": {"id": 1, "drop": []}
  })");
  Process ("domob", R"({
    "c": {"id": 2, "pu": {"f": {}}}
  })");
  Process ("domob", R"({
    "c": {"id": 1, "drop": {"f": {"foo": 0}}}
  })");
  Process ("domob", R"({
    "c": {"id": 1, "drop": {"f": {"invalid item": 1}}}
  })");
  Process ("domob", R"({
    "c": {"id": 1, "pu": {"f": {"invalid item": 1}}}
  })");

  /* Valid drop/pickup commands (character 1 will pickup, character 2 will
     drop, but not the corresponding other command).  */
  Process ("domob", R"({
    "c": {"id": 1, "pu": {"f": {"foo": 1}}}
  })");
  Process ("domob", R"({
    "c": {"id": 2, "drop": {"f": {"bar": 10}}}
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  auto b = buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  const auto bId = b->GetId ();
  b->MutableProto ().set_foundation (true);
  Inventory (*b->MutableProto ().mutable_construction_inventory ())
      .AddFungibleCount ("foo", 10);
  b.reset ();

  db.SetNextId (101);
  characters.CreateNew ("domob", Faction::RED)->SetBuildingId (bId);

  Process ("domob", R"({
    "c": {"id": 101, "pu": {"f": {"foo": 100}}}
  })");
  ExpectStateJson (R"(
    {
      "characters": []
    }
  )");
}

TEST_F (PendingStateUpdaterTests, Prospecting)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  const HexCoord pos(456, -789);
  auto h = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (h->GetId (), 1);
  h->SetPosition (pos);
  h->MutableProto ().set_prospecting_blocks (10);
  h.reset ();

  h = characters.CreateNew ("domob", Faction::GREEN);
  ASSERT_EQ (h->GetId (), 2);
  h->MutableProto ().set_ongoing (42);
  h->MutableProto ().set_prospecting_blocks (10);
  h.reset ();

  h = characters.CreateNew ("domob", Faction::BLUE);
  ASSERT_EQ (h->GetId (), 3);
  h->MutableProto ().clear_prospecting_blocks ();
  h.reset ();

  Process ("domob", R"({
    "c":
      [
        {"id": 1, "prospect": {}},
        {"id": 2, "prospect": {}},
        {"id": 3, "prospect": {}}
      ]
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

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
      [
        {"id": 1, "mine": {}},
        {"id": 2, "mine": {}}
      ]
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

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
      [
        {"id": 1, "fb": {"t": "huesli", "rot": 3}},
        {"id": 2, "fb": {"t": "huesli", "rot": 0}}
      ]
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

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
    "c": {"id": 1, "v": ["chariot"]}
  })");
  ExpectStateJson (R"(
    {
      "characters": []
    }
  )");

  /* This one is valid.  */
  Process ("domob", R"({
    "c": {"id": 1, "v": "chariot"}
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
    "c": {"id": 1, "v": "rv st"}
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

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
  inv->GetInventory ().AddFungibleCount ("super scanner", 1);
  inv.reset ();

  /* Invalid move format, no fitments will be pending.  */
  Process ("domob", R"({
    "c": {"id": 1, "fit": [42]}
  })");
  ExpectStateJson (R"(
    {
      "characters": []
    }
  )");

  /* This one is valid and sets pending fitments.  */
  Process ("domob", R"({
    "c": {"id": 1, "fit": ["sword", "super scanner"]}
  })");
  ExpectStateJson (R"(
    {
      "characters":
        [
          {"fitments": ["sword", "super scanner"]}
        ]
    }
  )");

  /* This one is invalid (exceeding the complexity limit) and thus
     the previous one will remain.  */
  Process ("domob", R"({
    "c": {"id": 1, "fit": ["sword", "bow"]}
  })");
  ExpectStateJson (R"(
    {
      "characters":
        [
          {"fitments": ["sword", "super scanner"]}
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, CreationAndUpdateTogether)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  CHECK_EQ (characters.CreateNew ("domob", Faction::RED)->GetId (), 1);

  ProcessWithDevPayment ("domob", characterCost, R"({
    "nc": [{}],
    "c": {"id": 1, "wp": null}
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

TEST_F (PendingStateUpdaterTests, UninitialisedAndNonExistantAccount)
{
  /* This verifies that the pending processor is fine with (i.e. does not
     crash or something like that) non-existant and uninitialised accounts.
     The pending moves (even though they might be valid in some of these cases)
     are not tracked.  */

  Process ("domob", R"({
    "vc": {"x": 5}
  })");
  Process ("domob", R"({
    "a": {"init": {"faction": "r"}}
  })");

  ASSERT_EQ (accounts.GetByName ("domob"), nullptr);
  accounts.CreateNew ("domob");

  ProcessWithDevPayment ("domob", characterCost, R"({
    "a": {"init": {"faction": "r"}},
    "nc": [{}]
  })");

  ExpectStateJson (R"(
    {
      "accounts": [],
      "characters": [],
      "newcharacters": []
    }
  )");
}

TEST_F (PendingStateUpdaterTests, CoinTransferBurn)
{
  accounts.CreateNew ("domob")->AddBalance (100);
  accounts.CreateNew ("andy")->AddBalance (100);

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

TEST_F (PendingStateUpdaterTests, Minting)
{
  accounts.CreateNew ("domob");

  ProcessWithBurn ("domob", COIN, R"({
    "vc": {"m": {}}
  })");
  ProcessWithBurn ("domob", 2 * COIN, R"({
    "vc": {"m": {}}
  })");

  ExpectStateJson (R"(
    {
      "accounts":
        [
          {
            "name": "domob",
            "coinops":
              {
                "minted": 30000
              }
          }
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, ServiceOperations)
{
  auto a = accounts.CreateNew ("domob");
  a->SetFaction (Faction::RED);
  a->AddBalance (100);
  a.reset ();

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

TEST_F (PendingStateUpdaterTests, DexOperations)
{
  accounts.CreateNew ("domob");

  db.SetNextId (100);
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  buildingInv.Get (100, "domob")->GetInventory ()
      .AddFungibleCount ("foo", 10);

  Process ("domob", R"({
    "x":
      [
        {"b": 100, "i": "foo", "n": 3, "t": "andy"},
        {"x": "invalid"},
        {"b": 100, "i": "foo", "n": 100, "t": "andy"},
        {"b": 100, "i": "foo", "n": 4, "t": "daniel"}
      ]
  })");

  ExpectStateJson (R"(
    {
      "accounts":
        [
          {
            "name": "domob",
            "dexops":
              [
                {
                  "building": 100,
                  "op": "transfer",
                  "item": "foo",
                  "num": 3,
                  "to": "andy"
                },
                {
                  "building": 100,
                  "op": "transfer",
                  "item": "foo",
                  "num": 4,
                  "to": "daniel"
                }
              ]
          }
        ]
    }
  )");
}

TEST_F (PendingStateUpdaterTests, MobileRefining)
{
  auto a = accounts.CreateNew ("domob");
  a->SetFaction (Faction::RED);
  a->AddBalance (100);
  a.reset ();

  db.SetNextId (101);
  auto c = characters.CreateNew ("domob", Faction::RED);
  c->GetInventory ().AddFungibleCount ("test ore", 20);
  auto& pb = c->MutableProto ();
  pb.set_cargo_space (1000);
  pb.mutable_refining ()->mutable_input ()->set_percent (100);
  c.reset ();

  Process ("domob", R"({
    "c": {"id": 101, "ref": {"i": "test ore", "n": 6}}
  })");
  Process ("domob", R"({
    "c": {"id": 101, "ref": {"i": "test ore", "n": 7}}
  })");

  ExpectStateJson (R"(
    {
      "accounts":
        [
          {
            "name": "domob",
            "serviceops":
              [
                {
                  "building": null,
                  "character": 101,
                  "type": "refining",
                  "input": {"test ore": 6}
                }
              ]
          }
        ]
    }
  )");
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
