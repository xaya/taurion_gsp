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

#include "moveprocessor.hpp"

#include "jsonutils.hpp"
#include "protoutils.hpp"
#include "testutils.hpp"

#include "database/dbtest.hpp"

#include <gtest/gtest.h>

#include <json/json.h>

#include <sstream>
#include <string>

namespace pxd
{
namespace
{

class MoveProcessorTests : public DBTestWithSchema
{

protected:

  ContextForTesting ctx;
  DynObstacles dyn;

private:

  TestRandom rnd;
  MoveProcessor mvProc;

protected:

  AccountsTable accounts;

  explicit MoveProcessorTests ()
    : dyn(db), mvProc(db, dyn, rnd, ctx), accounts(db)
  {}

  /**
   * Processes an array of admin commands given as JSON string.
   */
  void
  ProcessAdmin (const std::string& str)
  {
    Json::Value cmd;
    std::istringstream in(str);
    in >> cmd;

    mvProc.ProcessAdmin (cmd);
  }

  /**
   * Processes the given data (which is passed as string and converted to
   * JSON before processing it).
   */
  void
  Process (const std::string& str)
  {
    Json::Value val;
    std::istringstream in(str);
    in >> val;

    mvProc.ProcessAll (val);
  }

  /**
   * Processes the given data as string, adding the given amount as payment
   * to the dev address for each entry.  This is a utility method to avoid the
   * need of pasting in the long "out" and dev-address parts.
   */
  void
  ProcessWithDevPayment (const std::string& str, const Amount amount)
  {
    Json::Value val;
    std::istringstream in(str);
    in >> val;

    for (auto& entry : val)
      entry["out"][ctx.Params ().DeveloperAddress ()] = AmountToJson (amount);

    mvProc.ProcessAll (val);
  }

};

/* ************************************************************************** */

TEST_F (MoveProcessorTests, InvalidDataFromXaya)
{
  EXPECT_DEATH (Process ("{}"), "isArray");

  EXPECT_DEATH (Process (R"(
    [{"name": "domob"}]
  )"), "isMember.*move");

  EXPECT_DEATH (Process (R"(
    [{"move": {}}]
  )"), "nameVal.isString");
  EXPECT_DEATH (Process (R"(
    [{"name": 5, "move": {}}]
  )"), "nameVal.isString");

  EXPECT_DEATH (Process (R"([{
    "name": "domob", "move": {},
    "out": {")" + ctx.Params ().DeveloperAddress () + R"(": false}
  }])"), "JSON value for amount is not double");
}

TEST_F (MoveProcessorTests, InvalidAdminFromXaya)
{
  EXPECT_DEATH (ProcessAdmin ("42"), "isArray");
  EXPECT_DEATH (ProcessAdmin ("false"), "isArray");
  EXPECT_DEATH (ProcessAdmin ("null"), "isArray");
  EXPECT_DEATH (ProcessAdmin ("{}"), "isArray");
  EXPECT_DEATH (ProcessAdmin ("[5]"), "isObject");
}

TEST_F (MoveProcessorTests, AllMoveDataAccepted)
{
  for (const auto& mvStr : {"5", "false", "\"foo\"", "{}"})
    {
      LOG (INFO) << "Testing move data (in valid array): " << mvStr;

      std::ostringstream fullMoves;
      fullMoves
          << R"([{"name": "test", "move": )"
          << mvStr
          << "}]";

      Process (fullMoves.str ());
    }
}

TEST_F (MoveProcessorTests, AllAdminDataAccepted)
{
  for (const auto& admStr : {"42", "[5]", "\"foo\"", "null"})
    {
      LOG (INFO) << "Testing admin data (in valid array): " << admStr;

      std::ostringstream fullAdm;
      fullAdm << R"([{"cmd": )" << admStr << "}]";

      ProcessAdmin (fullAdm.str ());
    }
}

/* ************************************************************************** */

using AccountUpdateTests = MoveProcessorTests;

TEST_F (AccountUpdateTests, Initialisation)
{
  Process (R"([
    {"name": "domob", "move": {"a": {"x": 42, "init": {"faction": "b"}}}}
  ])");

  auto a = accounts.GetByName ("domob");
  ASSERT_TRUE (a != nullptr);
  EXPECT_EQ (a->GetFaction (), Faction::BLUE);
}

TEST_F (AccountUpdateTests, InvalidInitialisation)
{
  Process (R"([
    {"name": "domob", "move": {"a": {"init": {"x": 1, "faction": "b"}}}},
    {"name": "domob", "move": {"a": {"init": {"faction": "x"}}}},
    {"name": "domob", "move": {"a": {"init": {"y": 5}}}},
    {"name": "domob", "move": {"a": {"init": false}}},
    {"name": "domob", "move": {"a": 42}}
  ])");

  EXPECT_TRUE (accounts.GetByName ("domob") == nullptr);
}

TEST_F (AccountUpdateTests, InitialisationOfExistingAccount)
{
  accounts.CreateNew ("domob", Faction::RED);

  Process (R"([
    {"name": "domob", "move": {"a": {"init": {"faction": "b"}}}}
  ])");

  auto a = accounts.GetByName ("domob");
  ASSERT_TRUE (a != nullptr);
  EXPECT_EQ (a->GetFaction (), Faction::RED);
}

TEST_F (AccountUpdateTests, InitialisationAndCharacterCreation)
{
  ProcessWithDevPayment (R"([
    {"name": "domob", "move":
      {
        "a": {"x": 42, "init": {"faction": "b"}},
        "nc": [{}]
      }
    }
  ])", ctx.Params ().CharacterCost ());

  CharacterTable characters(db);
  auto c = characters.GetById (1);
  ASSERT_TRUE (c != nullptr);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::BLUE);
}

/* ************************************************************************** */

class CharacterCreationTests : public MoveProcessorTests
{

protected:

  /** Character table used in the tests.  */
  CharacterTable tbl;

  CharacterCreationTests ()
    : tbl(db)
  {}

};

TEST_F (CharacterCreationTests, InvalidCommands)
{
  accounts.CreateNew ("domob", Faction::RED);

  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {}},
    {"name": "domob", "move": {"nc": 42}},
    {"name": "domob", "move": {"nc": [{"faction": "r"}]}}
  ])", ctx.Params ().CharacterCost ());

  auto res = tbl.QueryAll ();
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, AccountNotInitialised)
{
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": [{}]}}
  ])", ctx.Params ().CharacterCost ());

  auto res = tbl.QueryAll ();
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, ValidCreation)
{
  accounts.CreateNew ("domob", Faction::RED);
  accounts.CreateNew ("andy", Faction::BLUE);

  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": []}},
    {"name": "domob", "move": {"nc": [{}]}},
    {"name": "andy", "move": {"nc": [{}]}}
  ])", ctx.Params ().CharacterCost ());

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::RED);

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::BLUE);

  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, DevPayment)
{
  accounts.CreateNew ("domob", Faction::RED);
  accounts.CreateNew ("andy", Faction::GREEN);

  Process (R"([
    {"name": "domob", "move": {"nc": [{}]}}
  ])");
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": [{}]}}
  ])", ctx.Params ().CharacterCost () - 1);
  ProcessWithDevPayment (R"([
    {"name": "andy", "move": {"nc": [{}]}}
  ])", ctx.Params ().CharacterCost () + 1);

  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::GREEN);
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, Multiple)
{
  accounts.CreateNew ("domob", Faction::RED);

  ProcessWithDevPayment (R"([
    {
      "name": "domob",
      "move":
        {
          "nc": [{}, {}, {}]
        }
    }
  ])", 2 * ctx.Params ().CharacterCost ());

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetFaction (), Faction::RED);

  EXPECT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetFaction (), Faction::RED);

  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, CharacterLimit)
{
  accounts.CreateNew ("domob", Faction::RED);
  for (unsigned i = 0; i < ctx.Params ().CharacterLimit () - 1; ++i)
    tbl.CreateNew ("domob", Faction::RED);

  EXPECT_EQ (tbl.CountForOwner ("domob"), ctx.Params ().CharacterLimit () - 1);

  ProcessWithDevPayment (R"([
    {
      "name": "domob",
      "move":
        {
          "nc": [{}, {}]
        }
    }
  ])", 2 * ctx.Params ().CharacterCost ());

  EXPECT_EQ (tbl.CountForOwner ("domob"), ctx.Params ().CharacterLimit ());
}

/* ************************************************************************** */

class CharacterUpdateTests : public MoveProcessorTests
{

protected:

  CharacterTable tbl;

  /**
   * All CharacterUpdateTests will start with a test character already created.
   * We also ensure that it has the ID 1.
   */
  CharacterUpdateTests ()
    : tbl(db)
  {
    SetupCharacter (1, "domob");
  }

  /**
   * Retrieves a handle to the test character.
   */
  CharacterTable::Handle
  GetTest ()
  {
    auto h = tbl.GetById (1);
    CHECK (h != nullptr);
    return h;
  }

  /**
   * Inserts a new character with the given ID, name and owner.
   */
  CharacterTable::Handle
  SetupCharacter (const Database::IdT id, const std::string& owner)
  {
    if (accounts.GetByName (owner) == nullptr)
      accounts.CreateNew (owner, Faction::RED);

    db.SetNextId (id);
    tbl.CreateNew (owner, Faction::RED);

    auto h = tbl.GetById (id);
    CHECK (h != nullptr);
    CHECK_EQ (h->GetId (), id);
    CHECK_EQ (h->GetOwner (), owner);

    return h;
  }

  /**
   * Verifies that ownership of the characters with the given IDs matches
   * the given names.  This can be used as a simple proxy for determining
   * whether update commands were executed or not.
   */
  void
  ExpectCharacterOwners (const std::map<Database::IdT, std::string>& expected)
  {
    for (const auto& entry : expected)
      {
        auto h = tbl.GetById (entry.first);
        ASSERT_TRUE (h != nullptr)
            << "Character ID " << entry.first << " does not exist";
        ASSERT_EQ (h->GetOwner (), entry.second)
            << "Character ID " << entry.first << " has wrong owner";
      }
  }

};

TEST_F (CharacterUpdateTests, CreationAndUpdate)
{
  accounts.CreateNew ("daniel", Faction::RED);
  accounts.CreateNew ("andy", Faction::RED);

  ProcessWithDevPayment (R"([{
    "name": "domob",
    "move":
      {
        "nc": [{}],
        "c": {"1": {"send": "daniel"}, "2": {"send": "andy"}}
      }
  }])", ctx.Params ().CharacterCost ());

  /* Transfer and creation should work fine together for two different
     characters (but in the same move).  The character created in the same
     move should not be transferred.  */
  ExpectCharacterOwners ({{1, "daniel"}, {2, "domob"}});
}

TEST_F (CharacterUpdateTests, AccountNotInitialised)
{
  db.SetNextId (10);
  auto c = tbl.CreateNew ("unknown account", Faction::RED);
  ASSERT_EQ (c->GetId (), 10);
  c.reset ();

  ASSERT_EQ (accounts.GetByName ("unknown account"), nullptr);

  ProcessWithDevPayment (R"([{
    "name": "unknown account",
    "move":
      {
        "c": {"10": {"send": "domob"}}
      }
  }])", ctx.Params ().CharacterCost ());

  ExpectCharacterOwners ({{10, "unknown account"}});
}

TEST_F (CharacterUpdateTests, MultipleCharacters)
{
  SetupCharacter (10, "domob");
  SetupCharacter (11, "domob");
  SetupCharacter (12, "domob");
  SetupCharacter (13, "domob");
  SetupCharacter (14, "domob");

  accounts.CreateNew ("andy", Faction::RED);
  accounts.CreateNew ("bob", Faction::RED);
  accounts.CreateNew ("charly", Faction::RED);
  accounts.CreateNew ("mallory", Faction::RED);

  /* This whole command is invalid, because an ID is specified twice in it.  */
  Process (R"([{
    "name": "domob",
    "move":
      {
        "c":
          {
            "10,11": {"send": "bob"},
            "11,12": {"send": "andy"}
          }
      }
  }])");
  ExpectCharacterOwners ({{10, "domob"}, {11, "domob"}, {12, "domob"}});

  /* This command is valid, and should transfer all characters accordingly;
     the invalid ID array string is ignored, as is the update part whose
     value is not an object.  */
  Process (R"([{
    "name": "domob",
    "move":
      {
        "c":
          {
            "12,11": {"send": "bob"},
            " 11 ": {"send": "mallory"},
            "12": "not an object",
            "13,10": {"send": "andy"},
            "14": {"send": "charly"}
          }
      }
  }])");
  ExpectCharacterOwners ({
    {10, "andy"},
    {11, "bob"},
    {12, "bob"},
    {13, "andy"},
    {14, "charly"},
  });
}

/* We also test the relative order of updates within a single move
   transaction for multiple characters.  For that, we use prospecting,
   as this has interactions between the characters.  Hence that test is
   further down below, among the ProspectingMoveTests.  */

TEST_F (CharacterUpdateTests, ValidTransfer)
{
  accounts.CreateNew ("andy", Faction::RED);

  EXPECT_EQ (GetTest ()->GetOwner (), "domob");
  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"send": "andy"}}}
  }])");
  ExpectCharacterOwners ({{1, "andy"}});
}

TEST_F (CharacterUpdateTests, InvalidTransfer)
{
  accounts.CreateNew ("at limit", Faction::RED);
  for (unsigned i = 0; i < ctx.Params ().CharacterLimit (); ++i)
    tbl.CreateNew ("at limit", Faction::RED);

  accounts.CreateNew ("wrong faction", Faction::GREEN);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"send": false}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"send": "uninitialised account"}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"send": "wrong faction"}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"send": "at limit"}}}
    }
  ])");

  ExpectCharacterOwners ({{1, "domob"}});
}

TEST_F (CharacterUpdateTests, OwnerCheck)
{
  /* Verify that a later update works fine even if a previous character update
     (from the same move) failed due to the owner check.  */
  SetupCharacter (9, "andy");

  ExpectCharacterOwners ({{1, "domob"}, {9, "andy"}});
  Process (R"([{
    "name": "andy",
    "move": {"c": {"1": {"send": "andy"}, "9": {"send": "domob"}}}
  }])");
  ExpectCharacterOwners ({{1, "domob"}, {9, "domob"}});
}

TEST_F (CharacterUpdateTests, InvalidUpdate)
{
  accounts.CreateNew ("andy", Faction::RED);

  /* We want to test that one invalid update still allows for other
     updates (i.e. other characters) to be done successfully in the same
     move transaction.  Thus create another character with a later ID that
     we will use for successful updates.  */
  SetupCharacter (9, "domob");

  for (const std::string upd : {"\"1\": []", "\"1\": false",
                                R"(" ": {"send": "andy"})",
                                R"("5": {"send": "andy"})"})
    {
      ASSERT_EQ (tbl.GetById (9)->GetOwner (), "domob");
      Process (R"([{
        "name": "domob",
        "move": {"c":{
          )" + upd + R"(,
          "9": {"send": "andy"}
        }}
      }])");

      auto h = tbl.GetById (9);
      EXPECT_EQ (h->GetOwner (), "andy");
      h->SetOwner ("domob");
    }
}

TEST_F (CharacterUpdateTests, WhenBusy)
{
  auto h = GetTest ();
  h->SetBusy (2);
  h->MutableProto ().mutable_prospection ();
  h->MutableProto ().mutable_mining ();
  h.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"wp": [{"x": -3, "y": 4}]}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"mine": {}}}}
    }
  ])");

  h = GetTest ();
  /* The fresh prospect command should have been ignored.  If it were not,
     then busy would have been set to 10.  */
  EXPECT_EQ (h->GetBusy (), 2);
  EXPECT_FALSE (h->GetProto ().has_movement ());
  EXPECT_FALSE (h->GetProto ().mining ().active ());
}

TEST_F (CharacterUpdateTests, BasicWaypoints)
{
  /* Set up some stuff that will be cleared.  */
  auto h = GetTest ();
  h->MutableProto ().set_speed (1000);
  h->MutableVolatileMv ().set_partial_step (42);
  auto* mv = h->MutableProto ().mutable_movement ();
  mv->mutable_waypoints ()->Add ();
  mv->mutable_steps ()->Add ();
  h.reset ();

  /* Run some moves that are invalid one way or another.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"wp": "foo"}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"wp": {"x": 4, "y": 3}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"wp": [{"x": 4.5, "y": 3}]}}}
    },
    {
      "name": "andy",
      "move": {"c": {"1": {"wp": [{"x": 4, "y": 3}]}}}
    }
  ])");

  /* Verify that we still have the original stuff (i.e. the invalid moves
     had no effect at all).  */
  h = GetTest ();
  EXPECT_EQ (h->GetVolatileMv ().partial_step (), 42);
  EXPECT_EQ (h->GetProto ().movement ().waypoints_size (), 1);
  EXPECT_EQ (h->GetProto ().movement ().steps_size (), 1);
  h.reset ();

  /* Process a valid waypoints update move.  */
  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"wp": [{"x": -3, "y": 4}, {"x": 5, "y": 0}]}}}
  }])");

  /* Verify that the valid move had the expected effect.  */
  h = GetTest ();
  EXPECT_FALSE (h->GetVolatileMv ().has_partial_step ());
  EXPECT_EQ (h->GetProto ().movement ().steps_size (), 0);
  const auto& wp = h->GetProto ().movement ().waypoints ();
  ASSERT_EQ (wp.size (), 2);
  EXPECT_EQ (CoordFromProto (wp.Get (0)), HexCoord (-3, 4));
  EXPECT_EQ (CoordFromProto (wp.Get (1)), HexCoord (5, 0));
}

TEST_F (CharacterUpdateTests, EmptyWaypoints)
{
  auto h = GetTest ();
  h->MutableProto ().set_speed (1000);
  h->MutableProto ().mutable_movement ();
  h->MutableVolatileMv ().set_partial_step (42);
  h.reset ();

  GetTest ()->MutableVolatileMv ().set_partial_step (42);
  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"wp": []}}}
  }])");

  h = GetTest ();
  EXPECT_FALSE (h->GetVolatileMv ().has_partial_step ());
  EXPECT_FALSE (h->GetProto ().has_movement ());
}

TEST_F (CharacterUpdateTests, WaypointsWithZeroSpeed)
{
  auto h = GetTest ();
  h->MutableProto ().set_speed (0);
  h->MutableProto ().mutable_movement ();
  h->MutableVolatileMv ().set_partial_step (42);
  h.reset ();

  GetTest ()->MutableVolatileMv ().set_partial_step (42);
  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"wp": [{"x": -3, "y": 100}]}}}
  }])");

  /* With zero speed of the character, we should just "stop" it but not
     create any new movement proto with those waypoints.  */

  h = GetTest ();
  EXPECT_FALSE (h->GetVolatileMv ().has_partial_step ());
  EXPECT_FALSE (h->GetProto ().has_movement ());
}

TEST_F (CharacterUpdateTests, ChosenSpeedWithoutMovement)
{
  GetTest ()->MutableProto ().set_speed (1000);

  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"speed": 100}}}
  }])");

  EXPECT_FALSE (GetTest ()->GetProto ().has_movement ());
}

TEST_F (CharacterUpdateTests, ChosenSpeedWorks)
{
  GetTest ()->MutableProto ().set_speed (1000);

  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"wp": [{"x": 5, "y": 1}], "speed": 1000000}}}
  }])");
  EXPECT_EQ (GetTest ()->GetProto ().movement ().chosen_speed (), 1000000);

  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"speed": 1}}}
  }])");
  EXPECT_EQ (GetTest ()->GetProto ().movement ().chosen_speed (), 1);
}

TEST_F (CharacterUpdateTests, ChosenSpeedInvalid)
{
  GetTest ()->MutableProto ().set_speed (1000);
  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"wp": [{"x": 5, "y": 1}], "speed": 1000}}}
  }])");

  /* All of them are invalid in one way or another.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"speed": -5}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"speed": 5.2}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"speed": 0}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"speed": {}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"speed": 1000001}}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetProto ().movement ().chosen_speed (), 1000);
}

/* ************************************************************************** */

class DropPickupMoveTests : public CharacterUpdateTests
{

protected:

  GroundLootTable loot;

  /** Position we use for testing.  */
  const HexCoord pos;

  DropPickupMoveTests ()
    : loot(db), pos(1, 2)
  {
    GetTest ()->MutableProto ().set_cargo_space (1000);
    GetTest ()->SetPosition (pos);
  }

  /**
   * Sets counts for all the items in the map in the given inventory.
   */
  static void
  SetInventoryItems (Inventory& inv,
                     const std::map<std::string, Inventory::QuantityT>& items)
  {
    for (const auto& entry : items)
      inv.SetFungibleCount (entry.first, entry.second);
  }

  /**
   * Expects that the given inventory has all the listed items (and not
   * any more).
   */
  void
  ExpectInventoryItems (
      Inventory& inv,
      const std::map<std::string, Inventory::QuantityT>& expected)
  {
    const auto& actual = inv.GetFungible ();
    ASSERT_EQ (actual.size (), expected.size ());
    for (const auto& entry : expected)
      {
        const auto mit = actual.find (entry.first);
        ASSERT_TRUE (mit != actual.end ())
            << "No actual entry for " << entry.first;
        ASSERT_EQ (mit->second, entry.second)
            << "Count mismatch for " << entry.first;
      }
  }

};

TEST_F (DropPickupMoveTests, InvalidDrop)
{
  SetInventoryItems (GetTest ()->GetInventory (), {{"foo", 1}});

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"drop": 42}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"drop": {"f": []}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"drop": {"f": {"foo": 1}, "x": 2}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"drop": {"f": {"foo": 1000000001}}}}}
    }
  ])");

  ExpectInventoryItems (loot.GetByCoord (pos)->GetInventory (), {});
  ExpectInventoryItems (GetTest ()->GetInventory (), {{"foo", 1}});
}

TEST_F (DropPickupMoveTests, InvalidPickUp)
{
  SetInventoryItems (loot.GetByCoord (pos)->GetInventory (), {{"foo", 1}});

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"pu": 42}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"pu": {"f": []}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"pu": {"f": {"foo": 1}, "x": 2}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"pu": {"f": {"foo": 1000000001}}}}}
    }
  ])");

  ExpectInventoryItems (GetTest ()->GetInventory (), {});
  ExpectInventoryItems (loot.GetByCoord (pos)->GetInventory (), {{"foo", 1}});
}

TEST_F (DropPickupMoveTests, BasicDrop)
{
  SetInventoryItems (GetTest ()->GetInventory (), {
    {"foo", 10},
    {"bar", 5},
  });
  SetInventoryItems (loot.GetByCoord (pos)->GetInventory (), {{"foo", 42}});

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"drop": {"f": {
        "a": 2000000000,
        "foo": 2,
        "bar": 1,
        "x": 10
      }}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"drop": {"f": {"foo": 1}}}}}
    }
  ])");

  ExpectInventoryItems (GetTest ()->GetInventory (), {
    {"foo", 7},
    {"bar", 4},
  });
  ExpectInventoryItems (loot.GetByCoord (pos)->GetInventory (), {
    {"foo", 45},
    {"bar", 1},
  });
}

TEST_F (DropPickupMoveTests, BasicPickUp)
{
  SetInventoryItems (GetTest ()->GetInventory (), {{"foo", 10}});
  SetInventoryItems (loot.GetByCoord (pos)->GetInventory (), {{"foo", 42}});

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"pu": {"f": {
        "a": 2000000000,
        "foo": 2,
        "bar": 10
      }}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"pu": {"f": {"foo": 1}}}}}
    }
  ])");

  ExpectInventoryItems (GetTest ()->GetInventory (), {
    {"foo", 13},
  });
  ExpectInventoryItems (loot.GetByCoord (pos)->GetInventory (), {
    {"foo", 39},
  });
}

TEST_F (DropPickupMoveTests, TooMuch)
{
  SetInventoryItems (GetTest ()->GetInventory (), {{"foo", 10}});
  SetInventoryItems (loot.GetByCoord (pos)->GetInventory (), {{"bar", 5}});

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {
        "drop": {"f": {"foo": 100}},
        "pu": {"f": {"bar": 100}}
      }}}
    }
  ])");

  ExpectInventoryItems (GetTest ()->GetInventory (), {{"bar", 5}});
  ExpectInventoryItems (loot.GetByCoord (pos)->GetInventory (), {{"foo", 10}});
}

TEST_F (DropPickupMoveTests, CargoLimit)
{
  GetTest ()->MutableProto ().set_cargo_space (100);
  SetInventoryItems (loot.GetByCoord (pos)->GetInventory (), {
    {"bar", 1},
    {"foo", 100},
    {"zerospace", 5},
  });

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {
        "pu": {"f": {"bar": 1, "foo": 100, "zerospace": 100}}
      }}}
    }
  ])");

  ExpectInventoryItems (GetTest ()->GetInventory (), {
    {"bar", 1},
    {"foo", 8},
    {"zerospace", 5},
  });
  ExpectInventoryItems (loot.GetByCoord (pos)->GetInventory (), {{"foo", 92}});
}

TEST_F (DropPickupMoveTests, RelativeOrder)
{
  /* Drop should happen before pickup.  We verify this by dropping too much
     (i.e. all) and then picking up a specified quantity.  If we were to pick
     up first, we would then drop all instead.  */

  SetInventoryItems (GetTest ()->GetInventory (), {{"foo", 10}});
  SetInventoryItems (loot.GetByCoord (pos)->GetInventory (), {{"foo", 10}});

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {
        "drop": {"f": {"foo": 100}},
        "pu": {"f": {"foo": 3}}
      }}}
    }
  ])");

  ExpectInventoryItems (GetTest ()->GetInventory (), {{"foo", 3}});
}

TEST_F (DropPickupMoveTests, OrderOfItems)
{
  /* This tests the order in which items are processed (should be alphabetically
     increasing by their string names).  We do this by filling up the cargo
     space.  "bar" uses 20 space and "foo" 10.  So if we run into the space
     limit with "bar", we will still be able to pick up one more "foo"
     afterwards.  If the processing order were reverse, we would fill up the
     entire cargo just with foo's.  */

  GetTest ()->MutableProto ().set_cargo_space (115);
  SetInventoryItems (loot.GetByCoord (pos)->GetInventory (), {
    {"foo", 100},
    {"bar", 100},
  });

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {
        "pu": {"f": {"foo": 100, "bar": 100}}
      }}}
    }
  ])");

  ExpectInventoryItems (GetTest ()->GetInventory (), {
    {"bar", 5},
    {"foo", 1},
  });
}

/* ************************************************************************** */

class MoveTestsWithRegion : public CharacterUpdateTests
{

protected:

  RegionsTable regions;

  /** Position for the test character.  */
  const HexCoord pos;

  /** Region of the test position.  */
  const RegionMap::IdT region;

  MoveTestsWithRegion ()
    : regions(db), pos(-10, 42), region(ctx.Map ().Regions ().GetRegionId (pos))
  {
    GetTest ()->SetPosition (pos);
  }

};

using ProspectingMoveTests = MoveTestsWithRegion;

TEST_F (ProspectingMoveTests, Success)
{
  auto h = GetTest ();
  h->MutableVolatileMv ().set_partial_step (42);
  h->MutableProto ().mutable_movement ()->add_waypoints ();
  h.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {
        "wp": [{"x": 5, "y": -2}],
        "prospect": {}
      }}}
    }
  ])");

  h = GetTest ();
  EXPECT_EQ (h->GetBusy (), 10);
  EXPECT_TRUE (h->GetProto ().has_prospection ());
  EXPECT_FALSE (h->GetVolatileMv ().has_partial_step ());
  EXPECT_FALSE (h->GetProto ().has_movement ());

  auto r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  EXPECT_FALSE (r->GetProto ().has_prospection ());
}

TEST_F (ProspectingMoveTests, Reprospecting)
{
  ctx.SetChain (xaya::Chain::REGTEST);

  auto r = regions.GetById (region);
  r->MutableProto ().mutable_prospection ()->set_height (10);
  r.reset ();

  ctx.SetHeight (110);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {
        "prospect": {}
      }}}
    }
  ])");

  r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  EXPECT_FALSE (r->GetProto ().has_prospection ());
}

TEST_F (ProspectingMoveTests, Invalid)
{
  GetTest ()->MutableProto ().mutable_movement ()->add_waypoints ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": true}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": 1}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {"x": 42}}}}
    }
  ])");

  auto h = GetTest ();
  EXPECT_EQ (h->GetBusy (), 0);
  EXPECT_FALSE (h->GetProto ().has_prospection ());
  EXPECT_TRUE (h->GetProto ().has_movement ());

  auto r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_FALSE (r->GetProto ().has_prospection ());
}

TEST_F (ProspectingMoveTests, CannotProspectRegion)
{
  auto r = regions.GetById (region);
  r->MutableProto ().mutable_prospection ()->set_name ("foo");
  r.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");

  auto h = GetTest ();
  EXPECT_EQ (h->GetBusy (), 0);
  EXPECT_FALSE (h->GetProto ().has_prospection ());

  r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "foo");
}

TEST_F (ProspectingMoveTests, MultipleCharacters)
{
  accounts.CreateNew ("foo", Faction::RED);

  auto c = tbl.CreateNew ("foo", Faction::RED);
  ASSERT_EQ (c->GetId (), 2);
  c->SetPosition (pos);
  c.reset ();

  Process (R"([
    {
      "name": "foo",
      "move": {"c": {"2": {"prospect": {}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");

  c = tbl.GetById (1);
  ASSERT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetBusy (), 0);

  c = tbl.GetById (2);
  ASSERT_EQ (c->GetOwner (), "foo");
  EXPECT_EQ (c->GetBusy (), 10);

  auto r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 2);
}

TEST_F (ProspectingMoveTests, OrderOfCharactersInAMove)
{
  /* Character 9 will be processed before character 10, since we order by
     ID and not by string.  */
  SetupCharacter (9, "domob")->SetPosition (pos);
  SetupCharacter (10, "domob")->SetPosition (pos);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {
        "10": {"prospect": {}},
        "9": {"prospect": {}}
      }}
    }
  ])");

  auto h = tbl.GetById (9);
  EXPECT_TRUE (h->GetProto ().has_prospection ());
  h = tbl.GetById (10);
  EXPECT_FALSE (h->GetProto ().has_prospection ());

  EXPECT_EQ (regions.GetById (region)->GetProto ().prospecting_character (), 9);
}

/* ************************************************************************** */

class MiningMoveTests : public MoveTestsWithRegion
{

protected:

  MiningMoveTests ()
  {
    /* By default, we set up the conditions so that the character can mine
       at its current position.  Tests that want to make mining impossible can
       then selectively undo some of these.  */

    auto c = GetTest ();
    auto& pb = c->MutableProto ();
    pb.mutable_mining ()->mutable_rate ()->set_min (10);
    pb.mutable_mining ()->mutable_rate ()->set_max (10);
    pb.set_speed (1000);
    c.reset ();

    auto r = regions.GetById (region);
    r->MutableProto ().mutable_prospection ()->set_resource ("foo");
    r->SetResourceLeft (42);
    r.reset ();
  }

  /**
   * Sends a valid move to start mining with the test character.
   */
  void
  AttemptMining ()
  {
    Process (R"([
      {
        "name": "domob",
        "move": {"c": {
          "1": {"mine": {}}
        }}
      }
    ])");
  }

};

TEST_F (MiningMoveTests, WaypointsStopMining)
{
  GetTest ()->MutableProto ().mutable_mining ()->set_active (true);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {
        "1": {"wp": []}
      }}
    }
  ])");

  EXPECT_TRUE (GetTest ()->GetProto ().has_mining ());
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().has_active ());
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, WaypointsNoMiningData)
{
  GetTest ()->MutableProto ().clear_mining ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {
        "1": {"wp": []}
      }}
    }
  ])");

  EXPECT_FALSE (GetTest ()->GetProto ().has_mining ());
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, InvalidMoveJson)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {
        "1": {"mine": 123}
      }}
    },
    {
      "name": "domob",
      "move": {"c": {
        "1": {"mine": {"x": 5}}
      }}
    }
  ])");

  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, CannotMine)
{
  GetTest ()->MutableProto ().clear_mining ();
  AttemptMining ();
  EXPECT_FALSE (GetTest ()->GetProto ().has_mining ());
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, CharacterIsMoving)
{
  GetTest ()->MutableProto ().mutable_movement ()->add_waypoints ();
  AttemptMining ();
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, RegionNotProspected)
{
  regions.GetById (region)->MutableProto ().clear_prospection ();
  AttemptMining ();
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, NoResourceLeft)
{
  regions.GetById (region)->SetResourceLeft (0);
  AttemptMining ();
  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, Success)
{
  AttemptMining ();
  EXPECT_TRUE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, AlreadyMining)
{
  AttemptMining ();
  AttemptMining ();
  EXPECT_TRUE (GetTest ()->GetProto ().mining ().active ());
}

TEST_F (MiningMoveTests, MiningAndWaypointsInSameMove)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {
        "1": {"wp": [{"x": 5, "y": 10}], "mine": {}}
      }}
    }
  ])");

  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

/* ************************************************************************** */

class GodModeTests : public MoveProcessorTests
{

protected:

  CharacterTable tbl;
  GroundLootTable loot;

  GodModeTests ()
    : tbl(db), loot(db)
  {
    ctx.SetChain (xaya::Chain::REGTEST);
  }

};

TEST_F (GodModeTests, InvalidTeleport)
{
  accounts.CreateNew ("domob", Faction::RED);
  const auto id = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  ASSERT_EQ (id, 1);

  ProcessAdmin (R"([{"cmd": {
    "god": false
  }}])");
  ProcessAdmin (R"([{"cmd": {
    "god": {"teleport": {"1": {"x": 5, "y": 0, "z": 42}}}
  }}])");

  EXPECT_EQ (tbl.GetById (id)->GetPosition (), HexCoord (0, 0));
}

TEST_F (GodModeTests, Teleport)
{
  accounts.CreateNew ("domob", Faction::RED);
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  ASSERT_EQ (id, 1);
  c->MutableVolatileMv ().set_partial_step (42);
  c->MutableProto ().mutable_movement ();
  c.reset ();

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "teleport":
          {
            "2": {"x": 0, "y": 0},
            "1": {"x": 5, "y": -42}
          }
      },
    "foo": "bar"
  }}])");

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetPosition (), HexCoord (5, -42));
  EXPECT_FALSE (c->GetVolatileMv ().has_partial_step ());
  EXPECT_FALSE (c->GetProto ().has_movement ());
}

TEST_F (GodModeTests, SetHp)
{
  accounts.CreateNew ("domob", Faction::RED);
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  ASSERT_EQ (id, 1);
  c->MutableHP ().set_armour (50);
  c->MutableHP ().set_shield (20);
  auto& regen = c->MutableRegenData ();
  regen.mutable_max_hp ()->set_armour (50);
  regen.mutable_max_hp ()->set_shield (20);
  c.reset ();

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "sethp":
          {
            "2": {"a": 5},
            "1": {"a": 32, "s": 15, "ma": -5, "ms": false, "x": "y"}
          }
      }
  }}])");

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetHP ().armour (), 32);
  EXPECT_EQ (c->GetHP ().shield (), 15);
  EXPECT_EQ (c->GetRegenData ().max_hp ().armour (), 50);
  EXPECT_EQ (c->GetRegenData ().max_hp ().shield (), 20);
  c.reset ();

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "sethp":
          {
            "1": {"a": 1.5, "s": -15, "ma": 100, "ms": 90}
          }
      }
  }}])");

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetHP ().armour (), 32);
  EXPECT_EQ (c->GetHP ().shield (), 15);
  EXPECT_EQ (c->GetRegenData ().max_hp ().armour (), 100);
  EXPECT_EQ (c->GetRegenData ().max_hp ().shield (), 90);
  c.reset ();
}

TEST_F (GodModeTests, InvalidDropLoot)
{
  const HexCoord pos(1, 2);

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "drop": {"foo": 1}
      }
  }}])");
  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "drop":
          [
            10,
            [],
            {
              "pos": {"invalid hex": true},
              "fungible": {"foo": 10}
            },
            {
              "pos": {"x": 1, "y": 2},
              "fungible": [{"foo": 10}]
            },
            {
              "pos": {"x": 1, "y": 2},
              "fungible": {"foo": 10.5, "bar": -5, "baz": 0}
            },
            {
              "pos": {"x": 1, "y": 2},
              "fungible": {"foo": 10},
              "extra": "value"
            },
            {
              "pos": {"x": 1, "y": 2},
              "fungible": {"foo": 1000000001}
            }
          ]
      }
  }}])");

  EXPECT_TRUE (loot.GetByCoord (pos)->GetInventory ().IsEmpty ());
}

TEST_F (GodModeTests, ValidDropLoot)
{
  const HexCoord pos(1, 2);

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "drop":
          [
            10,
            {
              "pos": {"x": 1, "y": 2},
              "fungible": {"a": false, "foo": 10, "z": "x"}
            },
            {
              "pos": {"x": 1, "y": 2},
              "fungible": {"foo": 10, "bar": 1000000000}
            }
          ]
      }
  }}])");

  auto h = loot.GetByCoord (pos);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 20);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), MAX_ITEM_QUANTITY);
}

/**
 * Test fixture for god mode but set up on mainnet, so that god mode is
 * actually not allowed.
 */
class GodModeDisabledTests : public MoveProcessorTests
{

protected:

  /** Character table for use in the test.  */
  CharacterTable tbl;

  GodModeDisabledTests ()
    : tbl(db)
  {}

};

TEST_F (GodModeDisabledTests, Teleport)
{
  accounts.CreateNew ("domob", Faction::RED);
  const auto id = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  ASSERT_EQ (id, 1);

  ProcessAdmin (R"([{"cmd": {
    "god": {"teleport": {"1": {"x": 5, "y": -42}}}
  }}])");

  EXPECT_EQ (tbl.GetById (id)->GetPosition (), HexCoord (0, 0));
}

TEST_F (GodModeDisabledTests, SetHp)
{
  accounts.CreateNew ("domob", Faction::RED);
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  ASSERT_EQ (id, 1);
  c->MutableHP ().set_armour (50);
  c.reset ();

  ProcessAdmin (R"([{"cmd": {
    "god": {"sethp": {"1": {"a": 10}}}
  }}])");

  EXPECT_EQ (tbl.GetById (id)->GetHP ().armour (), 50);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
