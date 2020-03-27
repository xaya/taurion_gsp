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

#include "gamestatejson.hpp"

#include "protoutils.hpp"
#include "testutils.hpp"

#include "database/account.hpp"
#include "database/building.hpp"
#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/inventory.hpp"
#include "database/itemcounts.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"
#include "proto/character.pb.h"
#include "proto/region.pb.h"

#include <gtest/gtest.h>

#include <json/json.h>

#include <string>

namespace pxd
{
namespace
{

/* ************************************************************************** */

class GameStateJsonTests : public DBTestWithSchema
{

private:

  /** Parameter instance for testing.  */
  const Params params;

  /** GameStateJson instance used in testing.  */
  GameStateJson converter;

protected:

  /** Basemap instance for the test.  */
  BaseMap map;

  GameStateJsonTests ()
    : params(xaya::Chain::MAIN), converter(db, params, map)
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
    const Json::Value actual = converter.FullState ();
    VLOG (1) << "Actual JSON for the game state:\n" << actual;
    ASSERT_TRUE (PartialJsonEqual (actual, ParseJson (expectedStr)));
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

  tbl.CreateNew ("andy", Faction::GREEN)->SetBuildingId (100);

  ExpectStateJson (R"({
    "characters":
      [
        {
          "id": 1, "owner": "domob", "faction": "r",
          "speed": 750,
          "inbuilding": null,
          "position": {"x": -5, "y": 2}
        },
        {
          "id": 2, "owner": "andy", "faction": "g",
          "inbuilding": 100,
          "position": null
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, EnterBuilding)
{
  tbl.CreateNew ("domob", Faction::RED);
  tbl.CreateNew ("andy", Faction::BLUE)->SetEnterBuilding (5);

  ExpectStateJson (R"({
    "characters":
      [
        {
          "id": 1, "owner": "domob", "faction": "r",
          "enterbuilding": null
        },
        {
          "id": 2, "owner": "andy", "faction": "b",
          "enterbuilding": 5
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, ChosenSpeed)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->MutableProto ().mutable_movement ()->set_chosen_speed (1234);
  c.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "movement":
            {
              "chosenspeed": 1234
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, Waypoints)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->MutableVolatileMv ().set_partial_step (5);
  c->MutableVolatileMv ().set_blocked_turns (3);
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
              "blockedturns": 3,
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
  proto::TargetId t;
  t.set_id (5);
  t.set_type (proto::TargetId::TYPE_CHARACTER);
  c->SetTarget (t);
  c.reset ();

  c = tbl.CreateNew ("domob", Faction::GREEN);
  t.set_id (42);
  t.set_type (proto::TargetId::TYPE_BUILDING);
  c->SetTarget (t);
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
  attack->set_min_damage (2);
  attack->set_max_damage (10);
  attack = cd->add_attacks ();
  attack->set_range (1);
  attack->set_area (true);
  attack->set_min_damage (0);
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
                  {"range": 5, "area": false, "mindamage": 2, "maxdamage": 10},
                  {"range": 1, "area": true, "mindamage": 0, "maxdamage": 1}
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
  auto& regen = c->MutableRegenData ();
  regen.mutable_max_hp ()->set_armour (100);
  regen.mutable_max_hp ()->set_shield (10);
  regen.set_shield_regeneration_mhp (1001);
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

TEST_F (CharacterJsonTests, Inventory)
{
  auto h = tbl.CreateNew ("domob", Faction::RED);
  h->MutableProto ().set_cargo_space (1000);
  h->GetInventory ().SetFungibleCount ("foo", 5);
  h->GetInventory ().SetFungibleCount ("bar", 10);
  h.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "inventory":
            {
              "fungible":
                {
                  "foo": 5,
                  "bar": 10
                }
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, CargoSpace)
{
  auto h = tbl.CreateNew ("domob", Faction::RED);
  h->MutableProto ().set_cargo_space (1000);
  h->GetInventory ().SetFungibleCount ("foo", 35);
  h.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "cargospace":
            {
              "total": 1000,
              "used": 350,
              "free": 650
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, Mining)
{
  const HexCoord pos(10, -5);
  ASSERT_EQ (map.Regions ().GetRegionId (pos), 350146);

  tbl.CreateNew ("without mining", Faction::RED);

  auto h = tbl.CreateNew ("inactive mining", Faction::RED);
  h->MutableProto ().mutable_mining ()->mutable_rate ()->set_min (0);
  h->MutableProto ().mutable_mining ()->mutable_rate ()->set_max (5);
  h.reset ();

  h = tbl.CreateNew ("active mining", Faction::RED);
  h->SetPosition (pos);
  h->MutableProto ().mutable_mining ()->mutable_rate ()->set_min (10);
  h->MutableProto ().mutable_mining ()->mutable_rate ()->set_max (11);
  h->MutableProto ().mutable_mining ()->set_active (true);
  h.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "owner": "without mining",
          "mining": null
        },
        {
          "owner": "inactive mining",
          "mining":
            {
              "rate":
                {
                  "min": 0,
                  "max": 5
                },
              "active": false,
              "region": null
            }
        },
        {
          "owner": "active mining",
          "mining":
            {
              "rate":
                {
                  "min": 10,
                  "max": 11
                },
              "active": true,
              "region": 350146
            }
        }
      ]
  })");
}

TEST_F (CharacterJsonTests, DamageLists)
{
  const auto id1 = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  const auto id2 = tbl.CreateNew ("domob", Faction::GREEN)->GetId ();
  ASSERT_EQ (id2, 2);

  DamageLists dl(db, 0);
  dl.AddEntry (id1, id2);

  ExpectStateJson (R"({
    "characters":
      [
        {
          "faction": "r",
          "combat":
            {
              "attackers": [2]
            }
        },
        {
          "faction": "g",
          "combat":
            {
              "attackers": null
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

TEST_F (CharacterJsonTests, ArmourRepair)
{
  auto c = tbl.CreateNew ("domob", Faction::RED);
  c->SetBuildingId (20);
  c->SetBusy (42);
  c->MutableProto ().mutable_armour_repair ();
  c.reset ();

  ExpectStateJson (R"({
    "characters":
      [
        {
          "owner": "domob",
          "busy":
            {
              "blocks": 42,
              "operation": "armourrepair"
            }
        }
      ]
  })");
}

/* ************************************************************************** */

class AccountJsonTests : public GameStateJsonTests
{

protected:

  AccountsTable tbl;

  AccountJsonTests ()
    : tbl(db)
  {}

};

TEST_F (AccountJsonTests, KillsAndFame)
{
  tbl.CreateNew ("foo", Faction::RED)->MutableProto ().set_kills (10);
  tbl.CreateNew ("bar", Faction::BLUE)->MutableProto ().set_fame (42);

  ExpectStateJson (R"({
    "accounts":
      [
        {"name": "bar", "faction": "b", "kills": 0, "fame": 42},
        {"name": "foo", "faction": "r", "kills": 10, "fame": 100}
      ]
  })");
}

TEST_F (AccountJsonTests, Balance)
{
  tbl.CreateNew ("foo", Faction::RED);
  tbl.CreateNew ("bar", Faction::BLUE)->AddBalance (42);

  ExpectStateJson (R"({
    "accounts":
      [
        {"name": "bar", "faction": "b", "balance": 42},
        {"name": "foo", "faction": "r", "balance": 0}
      ]
  })");
}

/* ************************************************************************** */

class BuildingJsonTests : public GameStateJsonTests
{

protected:

  BuildingsTable tbl;
  BuildingInventoriesTable inv;

  BuildingJsonTests ()
    : tbl(db), inv(db)
  {}

};

TEST_F (BuildingJsonTests, Basic)
{
  auto h = tbl.CreateNew ("checkmark", "foo", Faction::RED);
  h->SetCentre (HexCoord (1, 2));
  h.reset ();

  ExpectStateJson (R"({
    "buildings":
      [
        {
          "id": 1,
          "type": "checkmark",
          "owner": "foo",
          "faction": "r",
          "centre": {"x": 1, "y": 2},
          "rotationsteps": 0,
          "tiles":
            [
              {"x": 1, "y": 2},
              {"x": 2, "y": 2},
              {"x": 1, "y": 3},
              {"x": 1, "y": 4}
            ]
        }
      ]
  })");
}

TEST_F (BuildingJsonTests, Ancient)
{
  tbl.CreateNew ("checkmark", "", Faction::ANCIENT);

  ExpectStateJson (R"({
    "buildings":
      [
        {
          "owner": null,
          "faction": "a"
        }
      ]
  })");
}

TEST_F (BuildingJsonTests, Inventories)
{
  ASSERT_EQ (tbl.CreateNew ("checkmark", "", Faction::ANCIENT)->GetId (), 1);

  inv.Get (1, "domob")->GetInventory ().SetFungibleCount ("foo", 2);
  inv.Get (42, "domob")->GetInventory ().SetFungibleCount ("foo", 100);
  inv.Get (1, "andy")->GetInventory ().SetFungibleCount ("bar", 1);
  ExpectStateJson (R"({
    "buildings":
      [
        {
          "id": 1,
          "inventories":
            {
              "andy": {"fungible": {"bar": 1}},
              "domob": {"fungible": {"foo": 2}}
            }
        }
      ]
  })");

  inv.Get (1, "domob")->GetInventory ().SetFungibleCount ("foo", 0);
  inv.Get (1, "andy")->GetInventory ().SetFungibleCount ("bar", 0);
  ExpectStateJson (R"({
    "buildings":
      [
        {
          "id": 1,
          "inventories":
            {
              "andy": null,
              "domob": null
            }
        }
      ]
  })");
}

TEST_F (BuildingJsonTests, ServiceFees)
{
  auto b = tbl.CreateNew ("checkmark", "daniel", Faction::RED);
  ASSERT_EQ (b->GetId (), 1);
  b->MutableProto ().set_service_fee_percent (42);
  b.reset ();

  ExpectStateJson (R"({
    "buildings":
      [
        {
          "id": 1,
          "servicefee": 42
        }
      ]
  })");
}

TEST_F (BuildingJsonTests, CombatData)
{
  ASSERT_EQ (tbl.CreateNew ("checkmark", "", Faction::ANCIENT)->GetId (), 1);

  auto h = tbl.CreateNew ("checkmark", "daniel", Faction::RED);
  ASSERT_EQ (h->GetId (), 2);
  auto* att = h->MutableProto ().mutable_combat_data ()->add_attacks ();
  att->set_range (5);
  att->set_min_damage (1);
  att->set_max_damage (2);
  h->MutableHP ().set_armour (42);
  h->MutableHP ().set_shield_mhp (1);
  auto& regen = h->MutableRegenData ();
  regen.set_shield_regeneration_mhp (1'001);
  regen.mutable_max_hp ()->set_armour (100);
  regen.mutable_max_hp ()->set_shield (50);
  proto::TargetId t;
  t.set_id (10);
  t.set_type (proto::TargetId::TYPE_CHARACTER);
  h->SetTarget (t);
  h.reset ();

  ExpectStateJson (R"({
    "buildings":
      [
        {
          "id": 1,
          "combat": {"target": null}
        },
        {
          "id": 2,
          "combat":
            {
              "hp":
                {
                  "max": {"armour": 100, "shield": 50},
                  "current": {"armour": 42, "shield": 0.001},
                  "regeneration": 1.001
                },
              "attacks":
                [
                  {"range": 5, "area": false, "mindamage": 1, "maxdamage": 2}
                ],
              "target": { "id": 10 }
            }
        }
      ]
  })");
}

/* ************************************************************************** */

class GroundLootJsonTests : public GameStateJsonTests
{

protected:

  GroundLootTable tbl;

  GroundLootJsonTests ()
    : tbl(db)
  {}

};

TEST_F (GroundLootJsonTests, Empty)
{
  ExpectStateJson (R"({
    "groundloot": []
  })");
}

TEST_F (GroundLootJsonTests, FungibleInventory)
{
  auto h = tbl.GetByCoord (HexCoord (1, 2));
  h->GetInventory ().SetFungibleCount ("foo", 5);
  h->GetInventory ().SetFungibleCount ("bar", 42);
  h->GetInventory ().SetFungibleCount ("", 100);
  h.reset ();

  h = tbl.GetByCoord (HexCoord (-1, 20));
  h->GetInventory ().SetFungibleCount ("foo", 10);
  h.reset ();

  ExpectStateJson (R"({
    "groundloot":
      [
        {
          "position": {"x": -1, "y": 20},
          "inventory":
            {
              "fungible":
                {
                  "foo": 10
                }
            }
        },
        {
          "position": {"x": 1, "y": 2},
          "inventory":
            {
              "fungible":
                {
                  "foo": 5,
                  "bar": 42,
                  "": 100
                }
            }
        }
      ]
  })");
}

/* ************************************************************************** */

class OngoingsJsonTests : public GameStateJsonTests
{

protected:

  OngoingsTable tbl;

  OngoingsJsonTests ()
    : tbl(db)
  {}

};

TEST_F (OngoingsJsonTests, Empty)
{
  ExpectStateJson (R"({
    "ongoings": []
  })");
}

TEST_F (OngoingsJsonTests, BasicData)
{
  auto op = tbl.CreateNew ();
  ASSERT_EQ (op->GetId (), 1);
  op->SetHeight (5);
  op->SetCharacterId (42);
  op->MutableProto ().mutable_prospection ();
  op.reset ();

  op = tbl.CreateNew ();
  op->SetHeight (10);
  op->SetBuildingId (50);
  op->MutableProto ().mutable_prospection ();
  op.reset ();

  ExpectStateJson (R"({
    "ongoings":
      [
        {
          "id": 1,
          "height": 5,
          "characterid": 42,
          "buildingid": null
        },
        {
          "id": 2,
          "height": 10,
          "characterid": null,
          "buildingid": 50
        }
      ]
  })");
}

TEST_F (OngoingsJsonTests, Prospection)
{
  auto op = tbl.CreateNew ();
  ASSERT_EQ (op->GetId (), 1);
  op->MutableProto ().mutable_prospection ();
  op.reset ();

  ExpectStateJson (R"({
    "ongoings":
      [
        {
          "id": 1,
          "operation": "prospecting"
        }
      ]
  })");
}

TEST_F (OngoingsJsonTests, ArmourRepair)
{
  auto op = tbl.CreateNew ();
  ASSERT_EQ (op->GetId (), 1);
  op->MutableProto ().mutable_armour_repair ();
  op.reset ();

  ExpectStateJson (R"({
    "ongoings":
      [
        {
          "id": 1,
          "operation": "armourrepair"
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
    : tbl(db, 1'042)
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
        {
          "id": 20,
          "prospection": null,
          "resource": null
        }
      ]
  })");
}

TEST_F (RegionJsonTests, Prospection)
{
  tbl.GetById (20)->MutableProto ().set_prospecting_character (42);

  auto r = tbl.GetById (10);
  auto* prosp = r->MutableProto ().mutable_prospection ();
  prosp->set_name ("bar");
  prosp->set_height (107);
  r.reset ();

  ExpectStateJson (R"({
    "regions":
      [
        {
          "id": 10,
          "prospection":
            {
              "name": "bar",
              "height": 107
            }
        },
        {"id": 20, "prospection": {"inprogress": 42}}
      ]
  })");
}

TEST_F (RegionJsonTests, MiningResource)
{
  auto r = tbl.GetById (10);
  r->MutableProto ().mutable_prospection ()->set_resource ("sand");
  r->SetResourceLeft (150);
  r.reset ();

  ExpectStateJson (R"({
    "regions":
      [
        {
          "id": 10,
          "resource":
            {
              "type": "sand",
              "amount": 150
            }
        }
      ]
  })");
}

/* ************************************************************************** */

class PrizesJsonTests : public GameStateJsonTests
{

protected:

  ItemCounts cnt;

  PrizesJsonTests ()
    : cnt(db)
  {}

};

TEST_F (PrizesJsonTests, Works)
{
  cnt.IncrementFound ("gold prize");
  for (unsigned i = 0; i < 10; ++i)
    cnt.IncrementFound ("bronze prize");

  ExpectStateJson (R"({
    "prizes":
      {
        "gold":
          {
            "number": 3,
            "probability": 200000,
            "found": 1,
            "available": 2
          },
        "silver":
          {
            "number": 5,
            "probability": 100000,
            "found": 0,
            "available": 5
          },
        "bronze":
          {
            "number": 10,
            "probability": 25000,
            "found": 10,
            "available": 0
          }
      }
  })");
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
