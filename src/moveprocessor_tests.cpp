/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2025  Autonomous Worlds Ltd

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

#include <xayautil/jsonutils.hpp>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <json/json.h>

#include <string>

namespace pxd
{

DECLARE_int32 (fork_height_gamestart);

namespace
{

class MoveProcessorTests : public DBTestWithSchema
{

private:

  TestRandom rnd;

protected:

  ContextForTesting ctx;
  AccountsTable accounts;

  explicit MoveProcessorTests ()
    : accounts(db)
  {}

  /**
   * Processes an array of admin commands given as JSON string.
   */
  void
  ProcessAdmin (const std::string& str)
  {
    DynObstacles dyn(db, ctx);
    MoveProcessor mvProc(db, dyn, rnd, ctx);
    mvProc.ProcessAdmin (ParseJson (str));
  }

  /**
   * Processes the given data (which is passed as string and converted to
   * JSON before processing it).
   */
  void
  Process (const std::string& str)
  {
    DynObstacles dyn(db, ctx);
    MoveProcessor mvProc(db, dyn, rnd, ctx);
    mvProc.ProcessAll (ParseJson (str));
  }

  /**
   * Processes the given data as string, adding the given amount as payment
   * to the dev address for each entry.  This is a utility method to avoid the
   * need of pasting in the long "out" and dev-address parts.
   */
  void
  ProcessWithDevPayment (const std::string& str, const Amount amount)
  {
    Json::Value val = ParseJson (str);
    for (auto& entry : val)
      entry["out"][ctx.RoConfig ()->params ().dev_addr ()]
          = xaya::ChiAmountToJson (amount);

    DynObstacles dyn(db, ctx);
    MoveProcessor mvProc(db, dyn, rnd, ctx);
    mvProc.ProcessAll (val);
  }

  /**
   * Processes the given data as string, adding the given amount as burn
   * payment to each entry.
   */
  void ProcessWithBurn (const std::string& str, const Amount amount)
  {
    Json::Value val = ParseJson (str);
    for (auto& entry : val)
      entry["out"][ctx.RoConfig ()->params ().burn_addr ()]
          = xaya::ChiAmountToJson (amount);

    DynObstacles dyn(db, ctx);
    MoveProcessor mvProc(db, dyn, rnd, ctx);
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
    "out": {")" + ctx.RoConfig ()->params ().dev_addr () + R"(": false}
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
    {"name": "domob", "move": {"a": {"init": {"faction": "a"}}}},
    {"name": "domob", "move": {"a": {"init": {"y": 5}}}},
    {"name": "domob", "move": {"a": {"init": false}}},
    {"name": "domob", "move": {"a": 42}}
  ])");

  /* Sending the moves should have created the account entry, but it
     should not have gotten a faction yet.  */
  auto a = accounts.GetByName ("domob");
  ASSERT_NE (a, nullptr);
  EXPECT_FALSE (a->IsInitialised ());
}

TEST_F (AccountUpdateTests, InitialisationOfExistingAccount)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

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
  ])", ctx.RoConfig ()->params ().character_cost () * COIN);

  CharacterTable characters(db);
  auto c = characters.GetById (1);
  ASSERT_TRUE (c != nullptr);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::BLUE);
}

/* ************************************************************************** */

class CoinOperationTests : public MoveProcessorTests
{

protected:

  /**
   * Expects that the balances of the given accounts are as stated.
   */
  void
  ExpectBalances (const std::map<std::string, Amount>& expected)
  {
    for (const auto& entry : expected)
      {
        auto a = accounts.GetByName (entry.first);
        if (a == nullptr)
          ASSERT_EQ (entry.second, 0);
        else
          ASSERT_EQ (a->GetBalance (), entry.second);
      }
  }

};

TEST_F (CoinOperationTests, Invalid)
{
  accounts.CreateNew ("domob")->AddBalance (100);

  Process (R"([
    {"name": "domob", "move": {"vc": 42}},
    {"name": "domob", "move": {"vc": []}},
    {"name": "domob", "move": {"vc": {}}},
    {"name": "domob", "move": {"vc": {"x": 10}}},
    {"name": "domob", "move": {"vc": {"b": -1}}},
    {"name": "domob", "move": {"vc": {"b": 1000000001}}},
    {"name": "domob", "move": {"vc": {"b": 999999999999999999}}},
    {"name": "domob", "move": {"vc": {"t": 42}}},
    {"name": "domob", "move": {"vc": {"t": "other"}}},
    {"name": "domob", "move": {"vc": {"t": {"other": -1}}}},
    {"name": "domob", "move": {"vc": {"t": {"other": 1000000001}}}},
    {"name": "domob", "move": {"vc": {"t": {"other": 1.999999}}}},
    {"name": "domob", "move": {"vc": {"t": {"other": 101}}}},
  ])");
  ProcessWithBurn (R"([
    {"name": "domob", "move": {"vc": {"m": {"x": "foo"}}}},
    {"name": "domob", "move": {"vc": {"m": null}}},
    {"name": "domob", "move": {"vc": {"m": []}}},
    {"name": "domob", "move": {"vc": {}}}
  ])", COIN);

  ExpectBalances ({{"domob", 100}, {"other", 0}});
}

TEST_F (CoinOperationTests, ExtraFieldsAreFine)
{
  accounts.CreateNew ("domob")->AddBalance (100);
  Process (R"([
    {"name": "domob", "move": {"vc": {"b": 10, "x": "foo"}}}
  ])");
  ExpectBalances ({{"domob", 90}});
}

TEST_F (CoinOperationTests, BurnAndTransfer)
{
  accounts.CreateNew ("domob")->AddBalance (100);

  /* Some of the burns and transfers are invalid.  They should just be ignored,
     without affecting the rest of the operation (valid parts).  */
  Process (R"([
    {"name": "domob", "move": {"vc":
      {
        "b": 10,
        "t": {"a": "invalid", "b": -5, "c": 1000, "second": 5, "third": 3}
      }}
    },
    {"name": "domob", "move": {"vc":
      {
        "b": 1000,
        "t": {"third": 2}
      }}
    },
    {"name": "second", "move": {"vc": {"b": 1}}}
  ])");

  ExpectBalances ({
    {"domob", 80},
    {"second", 4},
    {"third", 5},
  });
}

TEST_F (CoinOperationTests, BurnAll)
{
  accounts.CreateNew ("domob")->AddBalance (100);
  Process (R"([
    {"name": "domob", "move": {"vc": {"b": 100}}}
  ])");
  ExpectBalances ({{"domob", 0}});
}

TEST_F (CoinOperationTests, TransferAll)
{
  accounts.CreateNew ("domob")->AddBalance (100);
  Process (R"([
    {"name": "domob", "move": {"vc": {"t": {"other": 100}}}}
  ])");
  ExpectBalances ({{"domob", 0}, {"other", 100}});
}

TEST_F (CoinOperationTests, SelfTransfer)
{
  accounts.CreateNew ("domob")->AddBalance (100);
  Process (R"([
    {"name": "domob", "move": {"vc": {"t": {"domob": 90, "other": 20}}}}
  ])");
  ExpectBalances ({{"domob", 80}, {"other", 20}});
}

TEST_F (CoinOperationTests, Minting)
{
  accounts.CreateNew ("andy")->SetFaction (Faction::RED);

  ProcessWithBurn (R"([
    {"name": "domob", "move": {"vc": {"m": {}}}}
  ])", 1'000'000 * COIN);
  ProcessWithBurn (R"([
    {"name": "andy", "move": {"vc": {"m": {}}}}
  ])", 2 * COIN + 19'999);

  ExpectBalances ({{"domob", 10'000'000'000}});

  MoneySupply ms(db);
  EXPECT_EQ (ms.Get ("burnsale"), 10'000'010'000);
}

TEST_F (CoinOperationTests, BurnsaleBalance)
{
  ProcessWithBurn (R"([
    {"name": "domob", "move": {"vc": {"m": {}}}},
  ])", COIN / 10);
  ProcessWithBurn (R"([
    {"name": "domob", "move": {"vc": {"b": 10}}}
  ])", COIN);

  ExpectBalances ({{"domob", 990}});
  EXPECT_EQ (accounts.GetByName ("domob")->GetProto ().burnsale_balance (),
             1'000);
}

TEST_F (CoinOperationTests, MintBeforeBurnBeforeTransfer)
{
  ProcessWithBurn (R"([
    {"name": "domob", "move": {"vc":
      {
        "m": {},
        "b": 90,
        "t": {"other": 20}
      }}
    }
  ])", COIN / 100);

  ExpectBalances ({
    {"domob", 10},
    {"other", 0},
  });

  ProcessWithBurn (R"([
    {"name": "domob", "move": {"vc":
      {
        "m": {},
        "t": {"other": 20}
      }}
    }
  ])", COIN / 100);

  ExpectBalances ({
    {"domob", 10 + 80},
    {"other", 20},
  });

  MoneySupply ms(db);
  EXPECT_EQ (ms.Get ("burnsale"), 200);
}

TEST_F (CoinOperationTests, TransferOrder)
{
  accounts.CreateNew ("domob")->AddBalance (100);

  Process (R"([
    {"name": "domob", "move": {"vc":
      {
        "t": {"z": 10, "a": 101, "middle": 99}
      }}
    }
  ])");

  ExpectBalances ({
    {"domob", 1},
    {"a", 0},
    {"middle", 99},
    {"z", 0},
  });
}

/* ************************************************************************** */

class GameStartTests : public CoinOperationTests
{

protected:

  GameStartTests ()
  {
    FLAGS_fork_height_gamestart = 100;
  }

  ~GameStartTests ()
  {
    FLAGS_fork_height_gamestart = -1;
  }

};

TEST_F (GameStartTests, Before)
{
  ctx.SetHeight (99);
  accounts.CreateNew ("domob")->AddBalance (100);

  ProcessWithBurn (R"([
    {"name": "andy", "move": {"vc": {"m": {}}}}
  ])", COIN);
  Process (R"([
    {"name": "domob", "move": {"vc": {"b": 10}}},
    {"name": "domob", "move": {"vc": {"t": {"daniel": 5}}}},
    {"name": "domob", "move": {"a": {"init": {"faction": "r"}}}}
  ])");

  ExpectBalances ({
    {"domob", 85},
    {"daniel", 5},
    {"andy", 10'000},
  });

  EXPECT_FALSE (accounts.GetByName ("domob")->IsInitialised ());
}

TEST_F (GameStartTests, After)
{
  ctx.SetHeight (100);
  Process (R"([
    {"name": "domob", "move": {"a": {"init": {"faction": "r"}}}}
  ])");

  auto a = accounts.GetByName ("domob");
  EXPECT_TRUE (a->IsInitialised ());
  EXPECT_EQ (a->GetFaction (), Faction::RED);
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {}},
    {"name": "domob", "move": {"nc": 42}},
    {"name": "domob", "move": {"nc": [{"faction": "r"}]}}
  ])", ctx.RoConfig ()->params ().character_cost () * COIN);

  auto res = tbl.QueryAll ();
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, AccountNotInitialised)
{
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": [{}]}}
  ])", ctx.RoConfig ()->params ().character_cost () * COIN);

  auto res = tbl.QueryAll ();
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, ValidCreation)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  accounts.CreateNew ("andy")->SetFaction (Faction::BLUE);

  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": []}},
    {"name": "domob", "move": {"nc": [{}]}},
    {"name": "andy", "move": {"nc": [{}]}}
  ])", ctx.RoConfig ()->params ().character_cost () * COIN);

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

TEST_F (CharacterCreationTests, VChiAirdrop)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  accounts.CreateNew ("andy")->SetFaction (Faction::BLUE);

  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": [{}, {}]}},
    {"name": "andy", "move": {"nc": [{}]}},
    {"name": "andy", "move": {"nc": [{}]}}
  ])", 2 * ctx.RoConfig ()->params ().character_cost () * COIN);

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 2'000);
  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 2'000);
}

TEST_F (CharacterCreationTests, DevPayment)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  accounts.CreateNew ("andy")->SetFaction (Faction::GREEN);

  Process (R"([
    {"name": "domob", "move": {"nc": [{}]}}
  ])");
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": [{}]}}
  ])", ctx.RoConfig ()->params ().character_cost () * COIN - 1);
  ProcessWithDevPayment (R"([
    {"name": "andy", "move": {"nc": [{}]}}
  ])", ctx.RoConfig ()->params ().character_cost () * COIN + 1);

  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::GREEN);
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, Multiple)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  ProcessWithDevPayment (R"([
    {
      "name": "domob",
      "move":
        {
          "nc": [{}, {}, {}]
        }
    }
  ])", 2 * ctx.RoConfig ()->params ().character_cost () * COIN);

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
  const unsigned limit = ctx.RoConfig ()->params ().character_limit ();

  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  for (unsigned i = 0; i < limit - 1; ++i)
    tbl.CreateNew ("domob", Faction::RED)->SetPosition (HexCoord (i, 0));

  EXPECT_EQ (tbl.CountForOwner ("domob"), limit - 1);

  ProcessWithDevPayment (R"([
    {
      "name": "domob",
      "move":
        {
          "nc": [{}, {}]
        }
    }
  ])", 2 * ctx.RoConfig ()->params ().character_cost () * COIN);

  EXPECT_EQ (tbl.CountForOwner ("domob"), limit);
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
      accounts.CreateNew (owner)->SetFaction (Faction::RED);

    db.SetNextId (id);
    /* We have to place the character on a spot not yet taken by another,
       thus set a position based on the ID.  */
    tbl.CreateNew (owner, Faction::RED)->SetPosition (HexCoord (id, 1));

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
  accounts.CreateNew ("daniel")->SetFaction (Faction::RED);
  accounts.CreateNew ("andy")->SetFaction (Faction::RED);

  ProcessWithDevPayment (R"([{
    "name": "domob",
    "move":
      {
        "nc": [{}],
        "c": [{"id": 1, "send": "daniel"}, {"id": 2, "send": "andy"}]
      }
  }])", ctx.RoConfig ()->params ().character_cost () * COIN);

  /* Transfer and creation should work fine together for two different
     characters (but in the same move).  The character created in the same
     move should not be transferred.  */
  ExpectCharacterOwners ({{1, "daniel"}, {2, "domob"}});
}

TEST_F (CharacterUpdateTests, AccountNotInitialised)
{
  db.SetNextId (10);
  auto c = tbl.CreateNew ("unknown account", Faction::GREEN);
  ASSERT_EQ (c->GetId (), 10);
  c.reset ();

  ASSERT_EQ (accounts.GetByName ("unknown account"), nullptr);

  ProcessWithDevPayment (R"([{
    "name": "unknown account",
    "move":
      {
        "c": {"id": 10, "send": "domob"}
      }
  }])", ctx.RoConfig ()->params ().character_cost () * COIN);

  ExpectCharacterOwners ({{10, "unknown account"}});
}

TEST_F (CharacterUpdateTests, MultipleCharacters)
{
  SetupCharacter (10, "domob");
  SetupCharacter (11, "domob");
  SetupCharacter (12, "domob");
  SetupCharacter (13, "domob");
  SetupCharacter (14, "domob");

  accounts.CreateNew ("andy")->SetFaction (Faction::RED);
  accounts.CreateNew ("bob")->SetFaction (Faction::RED);
  accounts.CreateNew ("charly")->SetFaction (Faction::RED);
  accounts.CreateNew ("mallory")->SetFaction (Faction::RED);

  /* This command is valid, and should transfer all characters accordingly;
     the invalid ID array string is ignored, and also later sends of the
     same character are invalid.  */
  Process (R"([{
    "name": "domob",
    "move":
      {
        "c":
          [
            {"id": [12, 11], "send": "bob"},
            {"id": "11", "send": "mallory"},
            {"id": [13, 10], "send": "andy"},
            {"id": 14, "send": "charly"}
          ]
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
  accounts.CreateNew ("andy")->SetFaction (Faction::RED);

  EXPECT_EQ (GetTest ()->GetOwner (), "domob");
  Process (R"([{
    "name": "domob",
    "move": {"c": {"id": 1, "send": "andy"}}
  }])");
  ExpectCharacterOwners ({{1, "andy"}});
}

TEST_F (CharacterUpdateTests, InvalidTransfer)
{
  accounts.CreateNew ("at limit")->SetFaction (Faction::RED);
  for (unsigned i = 0; i < ctx.RoConfig ()->params ().character_limit (); ++i)
    tbl.CreateNew ("at limit", Faction::RED)->SetPosition (HexCoord (i, 0));

  accounts.CreateNew ("uninitialised account");
  accounts.CreateNew ("wrong faction")->SetFaction (Faction::GREEN);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "send": false}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "send": "uninitialised account"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "send": "non-existant account"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "send": "wrong faction"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "send": "at limit"}}
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
    "move": {"c": [
      {"id": 1, "send": "andy"},
      {"id": 9, "send": "domob"}
    ]}
  }])");
  ExpectCharacterOwners ({{1, "domob"}, {9, "domob"}});
}

TEST_F (CharacterUpdateTests, InvalidUpdate)
{
  accounts.CreateNew ("andy")->SetFaction (Faction::RED);

  /* We want to test that one invalid update still allows for other
     updates (i.e. other characters) to be done successfully in the same
     move transaction.  Thus create another character that we will use
     for successful updates.  */
  SetupCharacter (9, "domob");

  for (const std::string upd : {"[]", "false",
                                R"({"id": " ", "send": "andy"})",
                                R"({"id": 5, "send": "andy"})"})
    {
      ASSERT_EQ (tbl.GetById (9)->GetOwner (), "domob");
      Process (R"([{
        "name": "domob",
        "move": {"c":[
          )" + upd + R"(,
          {"id": 9, "send": "andy"}
        ]}
      }])");

      auto h = tbl.GetById (9);
      EXPECT_EQ (h->GetOwner (), "andy");
      h->SetOwner ("domob");
    }

  /* Also within a single ID list, individual invalid IDs will be ignored
     and the rest still tried.  */
  ASSERT_EQ (tbl.GetById (9)->GetOwner (), "domob");
  Process (R"([{
    "name": "domob",
    "move": {"c": {
      "id": [false, 5, " ", null, 9],
      "send": "andy"
    }}
  }])");

  auto h = tbl.GetById (9);
  EXPECT_EQ (h->GetOwner (), "andy");
  h->SetOwner ("domob");
}

TEST_F (CharacterUpdateTests, WhenBusy)
{
  auto h = GetTest ();
  h->MutableProto ().set_ongoing (42);
  h->MutableProto ().mutable_mining ();
  h->MutableProto ().set_prospecting_blocks (10);
  h.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wp": )" + WpStr ({HexCoord (-3, 4)}) + R"(}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "prospect": {}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "mine": {}}}
    }
  ])");

  h = GetTest ();
  /* The fresh prospect command should have been ignored.  If it were not,
     then the ongoing ID would have been set to something else.  */
  EXPECT_EQ (h->GetProto ().ongoing (), 42);
  EXPECT_FALSE (h->GetProto ().has_movement ());
  EXPECT_FALSE (h->GetProto ().mining ().active ());
}

TEST_F (CharacterUpdateTests, InvalidWhenInsideBuilding)
{
  auto h = GetTest ();
  h->SetBuildingId (10);
  h->MutableProto ().mutable_mining ();
  h->MutableProto ().set_prospecting_blocks (10);
  h.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wp": )" + WpStr ({HexCoord (-3, 4)}) + R"(}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "prospect": {}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "mine": {}}}
    }
  ])");

  h = GetTest ();
  EXPECT_FALSE (h->IsBusy ());
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
  h.reset ();

  /* Run some moves that are invalid one way or another.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wp": "foo"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wp": []}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wp": {"x": 4, "y": 3}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wp": [{"x": 4, "y": 3}]}}
    },
    {
      "name": "andy",
      "move": {"c": {"id": 1, "wp": 42}}
    }
  ])");

  /* Verify that we still have the original stuff (i.e. the invalid moves
     had no effect at all).  */
  h = GetTest ();
  EXPECT_EQ (h->GetVolatileMv ().partial_step (), 42);
  EXPECT_EQ (h->GetProto ().movement ().waypoints_size (), 1);
  h.reset ();

  /* Process a valid waypoints update move.  */
  Process (R"([{
    "name": "domob",
    "move": {"c": {"id": 1, "wp": )"
        + WpStr ({HexCoord (-3, 4), HexCoord (5, 0)}) + R"(}}
  }])");

  /* Verify that the valid move had the expected effect.  */
  h = GetTest ();
  EXPECT_FALSE (h->GetVolatileMv ().has_partial_step ());
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
    "move": {"c": {"id": 1, "wp": null}}
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
    "move": {"c": {"id": 1, "wp": )" + WpStr ({HexCoord (-3, 100)}) + R"(}}
  }])");

  /* With zero speed of the character, we should just "stop" it but not
     create any new movement proto with those waypoints.  */

  h = GetTest ();
  EXPECT_FALSE (h->GetVolatileMv ().has_partial_step ());
  EXPECT_FALSE (h->GetProto ().has_movement ());
}

TEST_F (CharacterUpdateTests, WaypointExtension)
{
  auto h = GetTest ();
  h->MutableProto ().set_speed (1000);
  auto* mv = h->MutableProto ().mutable_movement ();
  mv->mutable_waypoints ()->Add ();
  h.reset ();

  /* Those are invalid.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wpx": "foo"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wpx": []}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wpx": null}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wpx": {"x": 4, "y": 3}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "wpx": [{"x": 4, "y": 3}]}}
    },
    {
      "name": "andy",
      "move": {"c": {"id": 1, "wpx": 42}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetProto ().movement ().waypoints_size (), 1);

  /* wpx is also invalid if there are not already waypoints.  */
  GetTest ()->MutableProto ().mutable_movement ()
      ->mutable_waypoints ()->Clear ();
  Process (R"([{
    "name": "domob",
    "move": {"c": {"id": 1, "wpx": )" + WpStr ({HexCoord (-3, 4)}) + R"(}}
  }])");
  EXPECT_TRUE (GetTest ()->GetProto ().movement ().waypoints ().empty ());

  /* Now the extension should work, even if we set the initial waypoints
     in the same move.  */
  Process (R"([{
    "name": "domob",
    "move": {"c": [
      {
        "id": 1,
        "wp": )" + WpStr ({HexCoord (-3, 4)}) + R"(,
        "wpx": )" + WpStr ({HexCoord (-4, 4)}) + R"(
      },
      {
        "id": 1,
        "wpx": )" + WpStr ({HexCoord (-4, 7)}) + R"(
      }
    ]}
  }])");
  h = GetTest ();
  const auto& wp = h->GetProto ().movement ().waypoints ();
  ASSERT_EQ (wp.size (), 3);
  EXPECT_EQ (CoordFromProto (wp.Get (0)), HexCoord (-3, 4));
  EXPECT_EQ (CoordFromProto (wp.Get (1)), HexCoord (-4, 4));
  EXPECT_EQ (CoordFromProto (wp.Get (2)), HexCoord (-4, 7));
}

TEST_F (CharacterUpdateTests, ChosenSpeedWithoutMovement)
{
  GetTest ()->MutableProto ().set_speed (1000);

  Process (R"([{
    "name": "domob",
    "move": {"c": {"id": 1, "speed": 100}}
  }])");

  EXPECT_FALSE (GetTest ()->GetProto ().has_movement ());
}

TEST_F (CharacterUpdateTests, ChosenSpeedWorks)
{
  GetTest ()->MutableProto ().set_speed (1000);

  Process (R"([{
    "name": "domob",
    "move": {"c": {"id": 1, "wp": )"
        + WpStr ({HexCoord (5, 1)})
        + R"(, "speed": 1000000}}
  }])");
  EXPECT_EQ (GetTest ()->GetProto ().movement ().chosen_speed (), 1000000);

  Process (R"([{
    "name": "domob",
    "move": {"c": {"id": 1, "speed": 1}}
  }])");
  EXPECT_EQ (GetTest ()->GetProto ().movement ().chosen_speed (), 1);
}

TEST_F (CharacterUpdateTests, ChosenSpeedInvalid)
{
  GetTest ()->MutableProto ().set_speed (1000);
  Process (R"([{
    "name": "domob",
    "move": {"c": {"id": 1, "wp": )"
        + WpStr ({HexCoord (5, 1)})
        + R"(, "speed": 1000}}
  }])");

  /* All of them are invalid in one way or another.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "speed": -5}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "speed": 5e0}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "speed": 5.2}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "speed": 0}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "speed": {}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "speed": 1000001}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetProto ().movement ().chosen_speed (), 1000);
}

/* ************************************************************************** */

class FitmentMoveTests : public CharacterUpdateTests
{

protected:

  static constexpr Database::IdT BUILDING = 100;

  BuildingsTable buildings;
  BuildingInventoriesTable inv;

  FitmentMoveTests ()
    : buildings(db), inv(db)
  {
    db.SetNextId (BUILDING);
    buildings.CreateNew ("ancient1", "", Faction::ANCIENT);

    auto c = GetTest ();
    c->SetBuildingId (BUILDING);

    auto& regen = c->MutableRegenData ();
    regen.mutable_max_hp ()->set_armour (100);
    regen.mutable_max_hp ()->set_shield (30);

    auto& hp = c->MutableHP ();
    hp.set_armour (regen.max_hp ().armour ());
    hp.set_shield (regen.max_hp ().shield ());
  }

  /**
   * Sets the test character's vehicle and existing fitments.
   */
  void
  SetVehicle (const std::string& vehicle,
              const std::vector<std::string>& fitments)
  {
    auto c = GetTest ();
    auto& pb = c->MutableProto ();
    pb.set_vehicle (vehicle);
    pb.clear_fitments ();
    for (const auto& f : fitments)
      pb.add_fitments (f);
  }

  /**
   * Expects that the fitments on the test character are as given.
   */
  void
  ExpectFitments (const std::vector<std::string>& expected)
  {
    std::vector<std::string> actual;
    auto c = GetTest ();
    for (const auto& f : c->GetProto ().fitments ())
      actual.push_back (f);

    ASSERT_EQ (actual, expected);
  }

  /**
   * Returns the inventory of the owner of the test character in our
   * test building.
   */
  BuildingInventoriesTable::Handle
  GetBuildingInv ()
  {
    return inv.Get (BUILDING, GetTest ()->GetOwner ());
  }

};

TEST_F (FitmentMoveTests, InvalidFormat)
{
  SetVehicle ("chariot", {"bow"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": {"bow": 5}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["sword", 42]}}
    }
  ])");

  ExpectFitments ({"bow"});
}

TEST_F (FitmentMoveTests, InvalidItems)
{
  SetVehicle ("chariot", {"bow"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("foo", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["invalid item"]}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["foo"]}}
    }
  ])");

  ExpectFitments ({"bow"});
}

TEST_F (FitmentMoveTests, NotInBuilding)
{
  SetVehicle ("chariot", {"bow"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 1);
  GetTest ()->SetPosition (HexCoord (42, 10));

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["sword"]}}
    }
  ])");

  ExpectFitments ({"bow"});
}

TEST_F (FitmentMoveTests, InFoundation)
{
  SetVehicle ("chariot", {"bow"});
  buildings.GetById (BUILDING)->MutableProto ().set_foundation (true);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": []}}
    }
  ])");

  ExpectFitments ({"bow"});
}

TEST_F (FitmentMoveTests, NoFullHp)
{
  SetVehicle ("chariot", {"bow"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 1);

  GetTest ()->MutableHP ().set_armour (30);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["sword"]}}
    }
  ])");

  GetTest ()->MutableHP ().set_armour (100);
  GetTest ()->MutableHP ().set_shield (10);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["sword"]}}
    }
  ])");

  ExpectFitments ({"bow"});
}

TEST_F (FitmentMoveTests, ItemsNotAvailable)
{
  SetVehicle ("chariot", {"bow"});

  /* Even though the inventory is auto-dropped before changing fitments,
     the item in the character inventory is not enough (because the check
     happens before the auto-drop).  */

  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 1);
  GetTest ()->GetInventory ().AddFungibleCount ("sword", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["sword", "sword"]}}
    }
  ])");

  ExpectFitments ({"bow"});
}

TEST_F (FitmentMoveTests, InvalidFitments)
{
  SetVehicle ("chariot", {"bow"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 10);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["bow", "sword"]}}
    }
  ])");

  ExpectFitments ({"bow"});
}

TEST_F (FitmentMoveTests, ValidUpdate)
{
  SetVehicle ("chariot", {"bow"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("super scanner", 2);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["super scanner"]}}
    }
  ])");

  ExpectFitments ({"super scanner"});
  auto inv = GetBuildingInv ();
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("super scanner"), 1);
  auto c = GetTest ();
  EXPECT_EQ (c->GetProto ().prospecting_blocks (), 1);
}

TEST_F (FitmentMoveTests, ExistingFitmentsReused)
{
  SetVehicle ("chariot", {"super scanner", "super scanner"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["sword", "super scanner"]}}
    }
  ])");

  ExpectFitments ({"sword", "super scanner"});
  auto inv = GetBuildingInv ();
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("super scanner"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("sword"), 0);
}

TEST_F (FitmentMoveTests, DropsInventory)
{
  SetVehicle ("chariot", {});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 1);
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 5);
  GetTest ()->GetInventory ().AddFungibleCount ("bar", 2);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fit": ["sword"]}}
    }
  ])");

  ExpectFitments ({"sword"});
  auto inv = GetBuildingInv ();
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("sword"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("foo"), 5);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bar"), 2);
  EXPECT_TRUE (GetTest ()->GetInventory ().IsEmpty ());
}

TEST_F (FitmentMoveTests, FitmentBeforePickup)
{
  SetVehicle ("chariot", {});
  GetTest ()->MutableProto ().set_cargo_space (100);
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "pu": {"f": {"sword": 1}},
          "fit": ["sword"]
        }
      }
    }
  ])");

  ExpectFitments ({"sword"});
  EXPECT_TRUE (GetBuildingInv ()->GetInventory ().IsEmpty ());
  EXPECT_TRUE (GetTest ()->GetInventory ().IsEmpty ());
}

using ChangeVehicleMoveTests = FitmentMoveTests;

TEST_F (ChangeVehicleMoveTests, InvalidFormat)
{
  SetVehicle ("rv st", {});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": 42}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": {"chariot": 1}}}
    }
  ])");

  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "rv st");
}

TEST_F (ChangeVehicleMoveTests, InvalidVehicle)
{
  SetVehicle ("rv st", {});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("foo", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "foo"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "invalid item"}}
    }
  ])");

  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "rv st");
}

TEST_F (ChangeVehicleMoveTests, NotInBuilding)
{
  SetVehicle ("rv st", {});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);
  GetTest ()->SetPosition (HexCoord (42, 10));

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");

  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "rv st");
}

TEST_F (ChangeVehicleMoveTests, InFoundation)
{
  SetVehicle ("rv st", {});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);
  buildings.GetById (BUILDING)->MutableProto ().set_foundation (true);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");

  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "rv st");
}

TEST_F (ChangeVehicleMoveTests, NoFullHp)
{
  SetVehicle ("rv st", {});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);

  GetTest ()->MutableHP ().set_armour (30);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");

  GetTest ()->MutableHP ().set_armour (100);
  GetTest ()->MutableHP ().set_shield (10);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");

  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "rv st");
}

TEST_F (ChangeVehicleMoveTests, MissingItem)
{
  SetVehicle ("rv st", {});

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");

  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "rv st");
}

TEST_F (ChangeVehicleMoveTests, BasicUpdate)
{
  SetVehicle ("rv st", {});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");

  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "chariot");
  auto inv = GetBuildingInv ();
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("chariot"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("rv st"), 1);
  auto c = GetTest ();
  EXPECT_EQ (c->GetRegenData ().max_hp ().armour (), 1'000);
  EXPECT_EQ (c->GetRegenData ().max_hp ().shield (), 100);
  EXPECT_EQ (c->GetHP ().armour (), 1'000);
  EXPECT_EQ (c->GetHP ().shield (), 100);
}

TEST_F (ChangeVehicleMoveTests, InventoryDropped)
{
  SetVehicle ("rv st", {});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 5);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");

  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "chariot");
  EXPECT_EQ (GetBuildingInv ()->GetInventory ().GetFungibleCount ("foo"), 5);
  EXPECT_TRUE (GetTest ()->GetInventory ().IsEmpty ());
}

TEST_F (ChangeVehicleMoveTests, RemovesFitments)
{
  SetVehicle ("rv st", {"bow"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");

  ExpectFitments ({});
  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "chariot");
  EXPECT_EQ (GetBuildingInv ()->GetInventory ().GetFungibleCount ("bow"), 1);
}

TEST_F (ChangeVehicleMoveTests, ChangeToSameType)
{
  /* If we try to change to the same type of vehicle, it only works if we
     have another of that type in the building inventory.  In other words,
     the vehicle we are in itself does not count.  So trying to change
     to that one again will not work, and thus not have the other effects
     (or dropping inventory or fitments).  */

  SetVehicle ("chariot", {"bow"});

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");
  ExpectFitments ({"bow"});

  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "v": "chariot"}}
    }
  ])");
  ExpectFitments ({});
}

TEST_F (ChangeVehicleMoveTests, ChangeVehicleBeforeFitmentsAndPickup)
{
  SetVehicle ("rv st", {"bow"});
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("chariot", 1);
  GetBuildingInv ()->GetInventory ().AddFungibleCount ("sword", 1);
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 1);

  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "fit": ["sword"],
          "pu": {"f": {"foo": 1}},
          "v": "chariot"
        }
      }
    }
  ])");

  ExpectFitments ({"sword"});
  EXPECT_EQ (GetTest ()->GetProto ().vehicle (), "chariot");
  auto inv = GetBuildingInv ();
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("chariot"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("rv st"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("sword"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("foo"), 0);
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("foo"), 1);
}

/* ************************************************************************** */

class FoundBuildingMoveTests : public CharacterUpdateTests
{

protected:

  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;

  FoundBuildingMoveTests ()
    : buildings(db), buildingInv(db)
  {
    db.SetNextId (101);
  }

};

TEST_F (FoundBuildingMoveTests, InvalidFormat)
{
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 10);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": "foo"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": 42, "rot": 5}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": -1}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": 4.0}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": 6}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": 0, "x": false}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "x": false}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"rot": 0}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "invalid building", "rot": 0}}}
    }
  ])");
  EXPECT_EQ (buildings.GetById (101), nullptr);
}

TEST_F (FoundBuildingMoveTests, CharacterBusy)
{
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 10);
  GetTest ()->MutableProto ().set_ongoing (1234);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": 0}}}
    }
  ])");
  EXPECT_EQ (buildings.GetById (101), nullptr);
}

TEST_F (FoundBuildingMoveTests, InBuilding)
{
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 10);
  GetTest ()->SetBuildingId (42);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": 0}}}
    }
  ])");
  EXPECT_EQ (buildings.GetById (101), nullptr);
}

TEST_F (FoundBuildingMoveTests, UnconstructibleBuilding)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "itemmaker", "rot": 0}}}
    }
  ])");
  EXPECT_EQ (buildings.GetById (101), nullptr);
}

TEST_F (FoundBuildingMoveTests, NotEnoughResources)
{
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 1);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": 0}}}
    }
  ])");
  EXPECT_EQ (buildings.GetById (101), nullptr);
}

TEST_F (FoundBuildingMoveTests, CannotPlaceBuilding)
{
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 10);
  db.SetNextId (10);
  tbl.CreateNew ("andy", Faction::GREEN)
      ->SetPosition (GetTest ()->GetPosition ());
  db.SetNextId (101);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": 0}}}
    }
  ])");
  EXPECT_EQ (buildings.GetById (101), nullptr);
}

TEST_F (FoundBuildingMoveTests, FactionCheck)
{
  ASSERT_EQ (GetTest ()->GetFaction (), Faction::RED);
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 10);

  db.SetNextId (101);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "g test", "rot": 0}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "r test", "rot": 0}}}
    }
  ])");

  auto b = buildings.GetById (101);
  ASSERT_NE (b, nullptr);
  EXPECT_EQ (b->GetType (), "r test");
  b.reset ();

  EXPECT_EQ (buildings.GetById (102), nullptr);
}

TEST_F (FoundBuildingMoveTests, Success)
{
  ctx.SetHeight (10);
  const HexCoord pos = GetTest ()->GetPosition ();
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 10);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "fb": {"t": "huesli", "rot": 3}}}
    }
  ])");

  auto b = buildings.GetById (101);
  ASSERT_NE (b, nullptr);
  EXPECT_EQ (b->GetType (), "huesli");
  EXPECT_EQ (b->GetOwner (), "domob");
  EXPECT_EQ (b->GetFaction (), Faction::RED);
  EXPECT_EQ (b->GetCentre (), pos);
  EXPECT_EQ (b->GetHP ().armour (), 10);

  const auto& pb = b->GetProto ();
  EXPECT_TRUE (pb.foundation ());
  EXPECT_EQ (pb.shape_trafo ().rotation_steps (), 3);
  EXPECT_EQ (pb.age_data ().founded_height (), 10);

  auto c = GetTest ();
  EXPECT_EQ (c->GetInventory ().GetFungibleCount ("foo"), 8);
  ASSERT_TRUE (c->IsInBuilding ());
  EXPECT_EQ (c->GetBuildingId (), b->GetId ());
}

TEST_F (FoundBuildingMoveTests, FoundationBeforeDrop)
{
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 2);
  GetTest ()->GetInventory ().AddFungibleCount ("zerospace", 10);
  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"zerospace": 100, "foo": 100}},
          "fb": {"t": "huesli", "rot": 3}
        }
      }
    }
  ])");

  ASSERT_NE (buildings.GetById (101), nullptr);
  EXPECT_TRUE (GetTest ()->GetInventory ().IsEmpty ());

  auto b = buildings.GetById (101);
  Inventory constructionInv(b->GetProto ().construction_inventory ());
  EXPECT_EQ (constructionInv.GetFungibleCount ("zerospace"), 10);
}

TEST_F (FoundBuildingMoveTests, FoundationBeforeExit)
{
  GetTest ()->GetInventory ().AddFungibleCount ("foo", 2);
  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "xb": {},
          "fb": {"t": "huesli", "rot": 3}
        }
      }
    }
  ])");

  ASSERT_NE (buildings.GetById (101), nullptr);
  EXPECT_FALSE (GetTest ()->IsInBuilding ());
}

/* ************************************************************************** */

class EnterBuildingMoveTests : public CharacterUpdateTests
{

protected:

  BuildingsTable buildings;
  GroundLootTable loot;
  BuildingInventoriesTable inv;

  EnterBuildingMoveTests ()
    : buildings(db), loot(db), inv(db)
  {}

};

TEST_F (EnterBuildingMoveTests, InvalidSet)
{
  db.SetNextId (100);
  auto b = buildings.CreateNew ("checkmark", "andy", Faction::GREEN);
  ASSERT_EQ (b->GetId (), 100);
  b.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": {}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": "foo"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": -10}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 0}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 42}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 100}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetEnterBuilding (), Database::EMPTY_ID);
}

TEST_F (EnterBuildingMoveTests, InvalidClear)
{
  GetTest ()->SetEnterBuilding (50);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": {}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": "foo"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 0}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 42}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetEnterBuilding (), 50);
}

TEST_F (EnterBuildingMoveTests, ValidEnter)
{
  db.SetNextId (100);
  auto b = buildings.CreateNew ("huesli", "domob", Faction::RED);
  ASSERT_EQ (b->GetId (), 100);
  b->SetCentre (HexCoord (2, 0));
  b = buildings.CreateNew ("huesli", "", Faction::ANCIENT);
  ASSERT_EQ (b->GetId (), 101);
  b->SetCentre (HexCoord (-2, 0));
  b.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 100}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetEnterBuilding (), 100);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 101}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetEnterBuilding (), 101);
}

TEST_F (EnterBuildingMoveTests, ValidClear)
{
  GetTest ()->SetEnterBuilding (50);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": null}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetEnterBuilding (), Database::EMPTY_ID);
}

TEST_F (EnterBuildingMoveTests, BusyIsFine)
{
  db.SetNextId (100);
  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  ASSERT_EQ (b->GetId (), 100);
  b.reset ();

  GetTest ()->MutableProto ().set_ongoing (42);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 100}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetEnterBuilding (), 100);
}

TEST_F (EnterBuildingMoveTests, AlreadyInBuilding)
{
  db.SetNextId (100);
  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  ASSERT_EQ (b->GetId (), 100);
  b.reset ();

  GetTest ()->SetBuildingId (42);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "eb": 100}}
    }
  ])");
  EXPECT_EQ (GetTest ()->GetEnterBuilding (), Database::EMPTY_ID);
}

using ExitBuildingMoveTests = EnterBuildingMoveTests;

TEST_F (ExitBuildingMoveTests, Invalid)
{
  GetTest ()->SetBuildingId (20);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": {"a": 10}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": "foo"}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": 20}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": null}}
    }
  ])");

  ASSERT_TRUE (GetTest ()->IsInBuilding ());
  EXPECT_EQ (GetTest ()->GetBuildingId (), 20);
}

TEST_F (ExitBuildingMoveTests, WhenBusy)
{
  const auto buildingId
      = buildings.CreateNew ("checkmark", "domob", Faction::RED)->GetId ();

  GetTest ()->SetBuildingId (buildingId);
  GetTest ()->MutableProto ().set_ongoing (42);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": {}}}
    }
  ])");

  ASSERT_TRUE (GetTest ()->IsInBuilding ());
  EXPECT_EQ (GetTest ()->GetBuildingId (), buildingId);
}

TEST_F (ExitBuildingMoveTests, NotInBuilding)
{
  const HexCoord pos(10, 20);
  GetTest ()->SetPosition (pos);

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": {}}}
    }
  ])");

  ASSERT_FALSE (GetTest ()->IsInBuilding ());
  EXPECT_EQ (GetTest ()->GetPosition (), pos);
}

TEST_F (ExitBuildingMoveTests, Valid)
{
  const HexCoord pos(10, 20);

  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  b->SetCentre (pos);
  GetTest ()->SetBuildingId (b->GetId ());
  b.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": {}}}
    }
  ])");

  ASSERT_FALSE (GetTest ()->IsInBuilding ());
  EXPECT_LE (HexCoord::DistanceL1 (GetTest ()->GetPosition (), pos), 5);
}

TEST_F (ExitBuildingMoveTests, EnterAndExitWhenInside)
{
  db.SetNextId (100);
  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  ASSERT_EQ (b->GetId (), 100);
  GetTest ()->SetBuildingId (100);
  b.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": {}, "eb": 100}}
    }
  ])");

  EXPECT_FALSE (GetTest ()->IsInBuilding ());
  EXPECT_EQ (GetTest ()->GetEnterBuilding (), Database::EMPTY_ID);
}

TEST_F (ExitBuildingMoveTests, InventoryBeforeExit)
{
  db.SetNextId (100);
  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  ASSERT_EQ (b->GetId (), 100);
  GetTest ()->SetBuildingId (100);
  b.reset ();

  GetTest ()->GetInventory ().SetFungibleCount ("foo", 1);

  /* The exit move will be processed last, so that the dropped items will
     end up in the building inventory and not on the ground.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "xb": {}, "drop": {"f": {"foo": 1}}}}
    }
  ])");

  EXPECT_FALSE (inv.Get (100, "domob")->GetInventory ().IsEmpty ());
  const auto pos = GetTest ()->GetPosition ();
  EXPECT_TRUE (loot.GetByCoord (pos)->GetInventory ().IsEmpty ());
}

/* ************************************************************************** */

class MobileRefiningMoveTests : public CharacterUpdateTests
{

private:

  GroundLootTable loot;
  const HexCoord pos;

protected:

  MobileRefiningMoveTests ()
    : loot(db), pos(1, 2)
  {
    SetupCharacter (1, "domob");
    auto t = GetTest ();

    accounts.GetByName (t->GetOwner ())->AddBalance (100);

    auto& pb = t->MutableProto ();
    pb.set_cargo_space (1'000);
    pb.mutable_refining ()->mutable_input ()->set_percent (100);

    t->GetInventory ().AddFungibleCount ("test ore", 20);
    t->SetPosition (pos);
  }

  /**
   * Returns an inventory handle for the ground loot at the test position.
   * We use this in tests about dropping / picking stuff up (i.e. order
   * of that relative to mobile refining).
   */
  GroundLootTable::Handle
  GetLoot ()
  {
    return loot.GetByCoord (pos);
  }

};

TEST_F (MobileRefiningMoveTests, Invalid)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "ref": {}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "ref": {"i": "test ore", "n": 10}}}
    }
  ])");
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 100);
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("test ore"), 20);
}

TEST_F (MobileRefiningMoveTests, Works)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "ref": {"i": "test ore", "n": 12}}}
    }
  ])");
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 80);
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("test ore"), 8);
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("bar"), 4);
  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("zerospace"), 2);
}

TEST_F (MobileRefiningMoveTests, BeforePickup)
{
  /* Refining one step uses up 6 "test ore" of total space 90, and
     yields outputs of total size 40.  This is done before processing a pickup
     action, which will then have free cargo available.  */

  auto c = GetTest ();
  c->MutableProto ().set_cargo_space (300);
  EXPECT_EQ (c->FreeCargoSpace (ctx.RoConfig ()), 0);
  c.reset ();

  GetLoot ()->GetInventory ().AddFungibleCount ("foo", 10);

  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "pu": {"f": {"foo": 10}},
          "ref": {"i": "test ore", "n": 6}
        }
      }
    }
  ])");

  EXPECT_EQ (GetTest ()->GetInventory ().GetFungibleCount ("foo"), 5);
}

TEST_F (MobileRefiningMoveTests, BeforeDrop)
{
  /* Refining is done before drop during move processing, so that it will
     be possible to drop the refining outputs right away, and only the
     not-yet-used-up inputs can be dropped.  */

  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"bar": 100, "test ore": 100}},
          "ref": {"i": "test ore", "n": 6}
        }
      }
    }
  ])");

  EXPECT_EQ (GetLoot ()->GetInventory ().GetFungibleCount ("bar"), 2);
  EXPECT_EQ (GetLoot ()->GetInventory ().GetFungibleCount ("test ore"), 14);
}

/* ************************************************************************** */

class DropPickupMoveTests : public CharacterUpdateTests
{

protected:

  GroundLootTable loot;
  BuildingsTable buildings;
  BuildingInventoriesTable inv;

  const HexCoord pos;

  DropPickupMoveTests ()
    : loot(db), buildings(db), inv(db), pos(1, 2)
  {
    GetTest ()->MutableProto ().set_cargo_space (1000);
    GetTest ()->SetPosition (pos);
  }

  /**
   * Sets counts for all the items in the map in the given inventory.
   */
  static void
  SetInventoryItems (Inventory& inv,
                     const std::map<std::string, Quantity>& items)
  {
    for (const auto& entry : items)
      inv.SetFungibleCount (entry.first, entry.second);
  }

  /**
   * Expects that the given inventory has all the listed items (and not
   * any more).
   */
  void
  ExpectInventoryItems (Inventory& inv,
                        const std::map<std::string, Quantity>& expected)
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
      "move": {"c": {"id": 1, "drop": 42}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "drop": {"f": []}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "drop": {"f": {"foo": 1}, "x": 2}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "drop": {"f": {"invalid item": 1}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "drop": {"f": {"foo": 10000000000000000}}}}
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
      "move": {"c": {"id": 1, "pu": 42}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "pu": {"f": []}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "pu": {"f": {"foo": 1}, "x": 2}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "pu": {"f": {"invalid item": 1}}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "pu": {"f": {"foo": 10000000000000000}}}}
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
      "move": {"c": {"id": 1, "drop": {"f": {
        "a": 2000000000,
        "foo": 2,
        "bar": 1,
        "x": 10
      }}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "drop": {"f": {"foo": 1}}}}
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
      "move": {"c": {"id": 1, "pu": {"f": {
        "a": 2000000000,
        "foo": 2,
        "bar": 10
      }}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "pu": {"f": {"foo": 1}}}}
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
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"foo": 100}},
          "pu": {"f": {"bar": 100}}
        }
      }
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
      "move": {"c":
        {
          "id": 1,
          "pu": {"f": {"bar": 1, "foo": 100, "zerospace": 100}}
        }
      }
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
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"foo": 100}},
          "pu": {"f": {"foo": 3}}
        }
      }
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
      "move": {"c":
        {
          "id": 1,
          "pu": {"f": {"foo": 100, "bar": 100}}
        }
      }
    }
  ])");

  ExpectInventoryItems (GetTest ()->GetInventory (), {
    {"bar", 5},
    {"foo", 1},
  });
}

TEST_F (DropPickupMoveTests, InBuilding)
{
  auto b = buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
  const auto bId = b->GetId ();
  const Database::IdT otherBuilding = 1234;
  b.reset ();

  GetTest ()->SetBuildingId (bId);

  SetInventoryItems (inv.Get (bId, "andy")->GetInventory (), {
    {"foo", 1},
    {"bar", 2},
  });
  SetInventoryItems (inv.Get (otherBuilding, "domob")->GetInventory (), {
    {"foo", 1},
    {"bar", 2},
  });
  SetInventoryItems (inv.Get (bId, "domob")->GetInventory (), {
    {"foo", 10},
    {"bar", 20},
  });
  SetInventoryItems (GetTest ()->GetInventory (), {
    {"foo", 30},
  });

  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"foo": 5}},
          "pu": {"f": {"bar": 3}}
        }
      }
    }
  ])");

  ExpectInventoryItems (loot.GetByCoord (pos)->GetInventory (), {});
  ExpectInventoryItems (inv.Get (bId, "andy")->GetInventory (), {
    {"bar", 2},
    {"foo", 1},
  });
  ExpectInventoryItems (inv.Get (otherBuilding, "domob")->GetInventory (), {
    {"bar", 2},
    {"foo", 1},
  });
  ExpectInventoryItems (inv.Get (bId, "domob")->GetInventory (), {
    {"bar", 17},
    {"foo", 15},
  });
  ExpectInventoryItems (GetTest ()->GetInventory (), {
    {"bar", 3},
    {"foo", 25},
  });
}

TEST_F (DropPickupMoveTests, DropInFoundation)
{
  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto bId = b->GetId ();
  b->MutableProto ().set_foundation (true);
  b.reset ();

  GetTest ()->SetBuildingId (bId);
  SetInventoryItems (GetTest ()->GetInventory (), {
    {"foo", 30},
  });

  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"foo": 5}}
        }
      }
    }
  ])");

  b = buildings.GetById (bId);
  Inventory constructionInv(b->GetProto ().construction_inventory ());
  ExpectInventoryItems (constructionInv, {{"foo", 5}});

  ExpectInventoryItems (inv.Get (bId, "domob")->GetInventory (), {});
  ExpectInventoryItems (GetTest ()->GetInventory (), {{"foo", 25}});
}

TEST_F (DropPickupMoveTests, PickupInFoundation)
{
  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  const auto bId = b->GetId ();
  b->MutableProto ().set_foundation (true);
  Inventory (*b->MutableProto ().mutable_construction_inventory ())
      .AddFungibleCount ("foo", 10);
  b.reset ();

  GetTest ()->SetBuildingId (bId);

  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "pu": {"f": {"foo": 10}}
        }
      }
    }
  ])");

  b = buildings.GetById (bId);
  Inventory constructionInv(b->GetProto ().construction_inventory ());
  ExpectInventoryItems (constructionInv, {{"foo", 10}});

  ExpectInventoryItems (GetTest ()->GetInventory (), {});
}

TEST_F (DropPickupMoveTests, StartBuildingConstruction)
{
  OngoingsTable ongoings(db);

  auto b = buildings.CreateNew ("huesli", "domob", Faction::RED);
  const auto bId = b->GetId ();
  b->MutableProto ().set_foundation (true);
  b.reset ();

  GetTest ()->SetBuildingId (bId);
  SetInventoryItems (GetTest ()->GetInventory (), {
    {"foo", 10},
    {"zerospace", 10},
  });

  /* This is not enough.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"foo": 5, "zerospace": 9}}
        }
      }
    }
  ])");
  EXPECT_FALSE (buildings.GetById (bId)
                  ->GetProto ().has_ongoing_construction ());
  ASSERT_FALSE (ongoings.QueryAll ().Step ());

  /* This will start construction.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"zerospace": 1}}
        }
      }
    }
  ])");
  EXPECT_TRUE (buildings.GetById (bId)
                  ->GetProto ().has_ongoing_construction ());

  /* Dropping more is fine but doesn't do anything else.  */
  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "drop": {"f": {"foo": 1}}
        }
      }
    }
  ])");

  auto res = ongoings.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto op = ongoings.GetFromResult (res);
  EXPECT_EQ (op->GetBuildingId (), bId);
  EXPECT_TRUE (op->GetProto ().has_building_construction ());
  EXPECT_FALSE (res.Step ());
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
    : regions(db, 1'042),
      pos(-10, 42), region(ctx.Map ().Regions ().GetRegionId (pos))
  {
    ctx.SetHeight (1'042);
    GetTest ()->SetPosition (pos);
    GetTest ()->MutableProto ().set_prospecting_blocks (10);
  }

};

class ProspectingMoveTests : public MoveTestsWithRegion
{

protected:

  OngoingsTable ongoings;

  ProspectingMoveTests ()
    : ongoings(db)
  {}

};

TEST_F (ProspectingMoveTests, Success)
{
  auto h = GetTest ();
  h->MutableVolatileMv ().set_partial_step (42);
  h->MutableProto ().mutable_movement ()->add_waypoints ();
  h.reset ();

  ctx.SetHeight (100);
  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "wp": )" + WpStr ({HexCoord (5, -2)}) + R"(,
          "prospect": {}
        }
      }
    }
  ])");

  h = GetTest ();
  EXPECT_TRUE (h->IsBusy ());
  EXPECT_FALSE (h->GetVolatileMv ().has_partial_step ());
  EXPECT_FALSE (h->GetProto ().has_movement ());

  auto op = ongoings.GetById (h->GetProto ().ongoing ());
  EXPECT_EQ (op->GetHeight (), 110);
  EXPECT_EQ (op->GetCharacterId (), 1);
  EXPECT_TRUE (op->GetProto ().has_prospection ());

  auto r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  EXPECT_FALSE (r->GetProto ().has_prospection ());
}

TEST_F (ProspectingMoveTests, Reprospecting)
{
  auto r = regions.GetById (region);
  r->MutableProto ().mutable_prospection ()->set_height (10);
  r.reset ();

  ctx.SetHeight (110);
  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": 1,
          "prospect": {}
        }
      }
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
      "move": {"c": {"id": 1, "prospect": true}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "prospect": 1}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "prospect": {"x": 42}}}
    }
  ])");

  auto h = GetTest ();
  EXPECT_FALSE (h->IsBusy ());
  EXPECT_TRUE (h->GetProto ().has_movement ());

  auto r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_FALSE (r->GetProto ().has_prospection ());
}

TEST_F (ProspectingMoveTests, CannotProspectRegion)
{
  auto r = regions.GetById (region);
  r->MutableProto ().mutable_prospection ()->set_height (10);
  r->MutableProto ().mutable_prospection ()->set_name ("foo");
  r.reset ();

  ctx.SetHeight (11);
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "prospect": {}}}
    }
  ])");

  auto h = GetTest ();
  EXPECT_FALSE (h->IsBusy ());

  r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "foo");
}

TEST_F (ProspectingMoveTests, NoProspectingAbility)
{
  GetTest ()->MutableProto ().clear_prospecting_blocks ();
  Process (R"([
    {
      "name": "domob",
      "move": {"c": {"id": 1, "prospect": {}}}
    }
  ])");
  EXPECT_FALSE (GetTest ()->IsBusy ());
}

TEST_F (ProspectingMoveTests, MultipleCharacters)
{
  accounts.CreateNew ("foo")->SetFaction (Faction::GREEN);

  auto c = tbl.CreateNew ("foo", Faction::GREEN);
  ASSERT_EQ (c->GetId (), 2);
  c->SetPosition (pos);
  c->MutableProto ().set_prospecting_blocks (10);
  c.reset ();

  Process (R"([
    {
      "name": "foo",
      "move": {"c": {"id": 2, "prospect": {}}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "prospect": {}}}
    }
  ])");

  c = tbl.GetById (1);
  ASSERT_EQ (c->GetOwner (), "domob");
  EXPECT_FALSE (c->IsBusy ());

  c = tbl.GetById (2);
  ASSERT_EQ (c->GetOwner (), "foo");
  EXPECT_TRUE (c->IsBusy ());

  auto r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 2);
}

TEST_F (ProspectingMoveTests, OrderOfCharactersInAMove)
{
  GetTest ()->SetPosition (HexCoord (0, 0));

  auto c = SetupCharacter (9, "domob");
  c->SetPosition (pos + HexCoord (1, 0));
  c->MutableProto ().set_prospecting_blocks (10);
  c.reset ();

  c = SetupCharacter (10, "domob");
  c->SetPosition (pos);
  c->MutableProto ().set_prospecting_blocks (10);
  c.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {"c":
        {
          "id": [10, 9],
          "prospect": {}
        }
      }
    }
  ])");

  EXPECT_FALSE (tbl.GetById (9)->IsBusy ());
  EXPECT_TRUE (tbl.GetById (10)->IsBusy ());

  EXPECT_EQ (regions.GetById (region)->GetProto ().prospecting_character (),
             10);
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
        "move": {"c": {"id": 1, "mine": {}}}
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
      "move": {"c": {"id": 1, "wp": null}}
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
      "move": {"c": {"id": 1, "wp": null}}
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
      "move": {"c": {"id": 1, "mine": 123}}
    },
    {
      "name": "domob",
      "move": {"c": {"id": 1, "mine": {"x": 5}}}
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
      "move": {"c":
        {
          "id": 1,
          "wp": )" + WpStr ({HexCoord (5, 10)}) + R"(,
          "mine": {}
        }
      }
    }
  ])");

  EXPECT_FALSE (GetTest ()->GetProto ().mining ().active ());
}

/* ************************************************************************** */

class BuildingUpdateTests : public MoveProcessorTests
{

protected:

  static constexpr Database::IdT ANCIENT = 100;
  static constexpr Database::IdT ANDY_OWNED = 101;
  static constexpr Database::IdT DOMOB_OWNED = 102;

  BuildingsTable buildings;
  OngoingsTable ongoings;

  BuildingUpdateTests ()
    : buildings(db), ongoings(db)
  {
    accounts.CreateNew ("domob")->SetFaction (Faction::RED);
    accounts.CreateNew ("andy")->SetFaction (Faction::RED);

    db.SetNextId (100);
    ctx.SetHeight (10);

    auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
    CHECK_EQ (b->GetId (), ANCIENT);
    b->SetCentre (HexCoord (100, 10));

    b = buildings.CreateNew ("checkmark", "andy", Faction::RED);
    CHECK_EQ (b->GetId (), ANDY_OWNED);
    b->SetCentre (HexCoord (-10, 10));

    b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
    CHECK_EQ (b->GetId (), DOMOB_OWNED);
    b->SetCentre (HexCoord (10, 10));
  }

  void
  ExpectNoOngoings ()
  {
    ASSERT_FALSE (ongoings.QueryAll ().Step ());
  }

};

constexpr Database::IdT BuildingUpdateTests::ANCIENT;
constexpr Database::IdT BuildingUpdateTests::ANDY_OWNED;
constexpr Database::IdT BuildingUpdateTests::DOMOB_OWNED;

TEST_F (BuildingUpdateTests, InvalidFormat)
{
  Process (R"([
    {"name": "domob", "move": {"b": null}},
    {"name": "domob", "move": {"b": 42}},
    {"name": "domob", "move": {"b": "foo"}},
    {
      "name": "domob",
      "move": {"b": {"id": "x", "sf": 10}}
    },
    {
      "name": "domob",
      "move": {"b": {"id": -50, "sf": 10}}
    }
  ])");

  EXPECT_FALSE (buildings.GetById (DOMOB_OWNED)->GetProto ().has_config ());
  ExpectNoOngoings ();
}

TEST_F (BuildingUpdateTests, NonExistantBuilding)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"b": {"id": 12345, "sf": 10}}
    }
  ])");
  EXPECT_FALSE (buildings.GetById (DOMOB_OWNED)->GetProto ().has_config ());
  ExpectNoOngoings ();
}

TEST_F (BuildingUpdateTests, AncientCannotBeUpdated)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"b": {"id": 100, "sf": 10}}
    }
  ])");
  EXPECT_FALSE (buildings.GetById (ANCIENT)->GetProto ().has_config ());
  ExpectNoOngoings ();
}

TEST_F (BuildingUpdateTests, NotOwner)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"b": {"id": 101, "sf": 10}}
    }
  ])");
  EXPECT_FALSE (buildings.GetById (ANDY_OWNED)->GetProto ().has_config ());
  ExpectNoOngoings ();
}

TEST_F (BuildingUpdateTests, ArrayUpdate)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"b": [
        {"id": 102, "sf": 10},
        {"id": 101, "sf": 50},
        {"id": 102, "sf": 70},
        {"id": [102], "sf": 80}
      ]}
    }
  ])");

  EXPECT_FALSE (buildings.GetById (ANDY_OWNED)->GetProto ().has_config ());
  EXPECT_FALSE (buildings.GetById (DOMOB_OWNED)->GetProto ().has_config ());

  auto res = ongoings.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto op = ongoings.GetFromResult (res);
  EXPECT_EQ (op->GetCharacterId (), Database::EMPTY_ID);
  EXPECT_EQ (op->GetBuildingId (), DOMOB_OWNED);
  EXPECT_EQ (op->GetHeight (), 20);
  const auto* upd = &op->GetProto ().building_update ();
  EXPECT_EQ (upd->new_config ().service_fee_percent (), 10);
  EXPECT_FALSE (upd->new_config ().has_dex_fee_bps ());

  ASSERT_TRUE (res.Step ());
  op = ongoings.GetFromResult (res);
  EXPECT_EQ (op->GetCharacterId (), Database::EMPTY_ID);
  EXPECT_EQ (op->GetBuildingId (), DOMOB_OWNED);
  EXPECT_EQ (op->GetHeight (), 20);
  upd = &op->GetProto ().building_update ();
  EXPECT_EQ (upd->new_config ().service_fee_percent (), 70);
  EXPECT_FALSE (upd->new_config ().has_dex_fee_bps ());

  EXPECT_FALSE (res.Step ());
}

TEST_F (BuildingUpdateTests, SetServiceFee)
{
  Process (R"([
    {
      "name": "andy",
      "move": {"b": {"id": 101, "sf": 1001}}
    },
    {
      "name": "andy",
      "move": {"b": {"id": 101, "sf": -20}}
    },
    {
      "name": "andy",
      "move": {"b": {"id": 101, "sf": 42.0}}
    },
    {
      "name": "andy",
      "move": {"b": {"id": 101, "sf": "42"}}
    }
  ])");
  EXPECT_FALSE (buildings.GetById (ANDY_OWNED)->GetProto ().has_config ());
  ExpectNoOngoings ();

  Process (R"([
    {
      "name": "andy",
      "move": {"b": {"id": 101, "x": 42, "sf": 1000}}
    },
    {
      "name": "domob",
      "move": {"b": {"id": 102, "sf": 0}}
    }
  ])");

  EXPECT_FALSE (buildings.GetById (ANDY_OWNED)->GetProto ().has_config ());
  EXPECT_FALSE (buildings.GetById (DOMOB_OWNED)->GetProto ().has_config ());

  auto res = ongoings.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto op = ongoings.GetFromResult (res);
  EXPECT_EQ (op->GetBuildingId (), ANDY_OWNED);
  const auto* upd = &op->GetProto ().building_update ();
  EXPECT_EQ (upd->new_config ().service_fee_percent (), 1'000);
  EXPECT_FALSE (upd->new_config ().has_dex_fee_bps ());

  ASSERT_TRUE (res.Step ());
  op = ongoings.GetFromResult (res);
  EXPECT_EQ (op->GetBuildingId (), DOMOB_OWNED);
  upd = &op->GetProto ().building_update ();
  EXPECT_TRUE (upd->new_config ().has_service_fee_percent ());
  EXPECT_EQ (upd->new_config ().service_fee_percent (), 0);
  EXPECT_FALSE (upd->new_config ().has_dex_fee_bps ());

  EXPECT_FALSE (res.Step ());
}

TEST_F (BuildingUpdateTests, SetDexFee)
{
  Process (R"([
    {
      "name": "andy",
      "move": {"b": {"id": 101, "xf": 3001}}
    },
    {
      "name": "andy",
      "move": {"b": {"id": 101, "xf": -20}}
    },
    {
      "name": "andy",
      "move": {"b": {"id": 101, "xf": 42.0}}
    },
    {
      "name": "andy",
      "move": {"b": {"id": 101, "xf": "42"}}
    }
  ])");
  EXPECT_FALSE (buildings.GetById (ANDY_OWNED)->GetProto ().has_config ());
  ExpectNoOngoings ();

  Process (R"([
    {
      "name": "andy",
      "move": {"b": {"id": 101, "xf": 3000}}
    },
    {
      "name": "domob",
      "move": {"b": {"id": 102, "xf": 0}}
    }
  ])");

  EXPECT_FALSE (buildings.GetById (ANDY_OWNED)->GetProto ().has_config ());
  EXPECT_FALSE (buildings.GetById (DOMOB_OWNED)->GetProto ().has_config ());

  auto res = ongoings.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto op = ongoings.GetFromResult (res);
  EXPECT_EQ (op->GetBuildingId (), ANDY_OWNED);
  const auto* upd = &op->GetProto ().building_update ();
  EXPECT_EQ (upd->new_config ().dex_fee_bps (), 3'000);
  EXPECT_FALSE (upd->new_config ().has_service_fee_percent ());

  ASSERT_TRUE (res.Step ());
  op = ongoings.GetFromResult (res);
  EXPECT_EQ (op->GetBuildingId (), DOMOB_OWNED);
  upd = &op->GetProto ().building_update ();
  EXPECT_TRUE (upd->new_config ().has_dex_fee_bps ());
  EXPECT_EQ (upd->new_config ().dex_fee_bps (), 0);
  EXPECT_FALSE (upd->new_config ().has_service_fee_percent ());

  EXPECT_FALSE (res.Step ());
}

TEST_F (BuildingUpdateTests, Transfer)
{
  accounts.CreateNew ("blue")->SetFaction (Faction::BLUE);
  accounts.CreateNew ("uninit");

  /* A bunch of invalid transfer commands.  */
  Process (R"([
    {
      "name": "andy",
      "move": {"b": [
        {"id": 101, "send": null},
        {"id": 101, "send": 42},
        {"id": 101, "send": "blue"},
        {"id": 101, "send": "uninit"},
        {"id": 101, "send": "nonexistant"},
        {"id": 101, "sf": 1}
      ]}
    }
  ])");
  EXPECT_EQ (buildings.GetById (ANDY_OWNED)->GetOwner (), "andy");

  auto res = ongoings.QueryAll ();
  ASSERT_TRUE (res.Step ());
  ASSERT_FALSE (res.Step ());

  /* Valid transfers (at least until the building is moved out).  */
  Process (R"([
    {
      "name": "andy",
      "move": {"b": [
        {"id": 101, "send": "andy"},
        {"id": 101, "sf": 2},
        {"id": 101, "send": "domob"},
        {"id": 101, "send": "andy"}
      ]}
    }
  ])");
  EXPECT_EQ (buildings.GetById (ANDY_OWNED)->GetOwner (), "domob");

  res = ongoings.QueryAll ();
  ASSERT_TRUE (res.Step ());
  ASSERT_TRUE (res.Step ());
  ASSERT_FALSE (res.Step ());
}

/* ************************************************************************** */

class ServicesMoveTests : public MoveProcessorTests
{

protected:

  BuildingsTable buildings;
  BuildingInventoriesTable inv;
  CharacterTable characters;

  ServicesMoveTests ()
    : buildings(db), inv(db), characters(db)
  {
    auto a = accounts.CreateNew ("domob");
    a->SetFaction (Faction::RED);
    a->AddBalance (100);
    a.reset ();

    db.SetNextId (100);
    buildings.CreateNew ("ancient1", "", Faction::ANCIENT)
        ->SetCentre (HexCoord (100, 0));
    buildings.CreateNew ("ancient2", "", Faction::ANCIENT)
        ->SetCentre (HexCoord (-100, 0));

    inv.Get (100, "domob")->GetInventory ().AddFungibleCount ("test ore", 3);
    inv.Get (101, "domob")->GetInventory ().AddFungibleCount ("test ore", 6);
  }

};

TEST_F (ServicesMoveTests, Works)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"s": [
        {"x": "invalid"},
        {"b": 100, "t": "ref", "i": "test ore", "n": 3},
        {"b": 101, "t": "ref", "i": "test ore", "n": 6}
      ]}
    }
  ])");

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 70);
  auto i = inv.Get (100, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("test ore"), 0);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("bar"), 2);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("zerospace"), 1);
  i = inv.Get (101, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("test ore"), 0);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("bar"), 4);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("zerospace"), 2);
}

TEST_F (ServicesMoveTests, InvalidMoves)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"s": {"b": 100, "t": "ref", "i": "test ore", "n": 3}}
    },
    {
      "name": "domob",
      "move": {"s": [
        {"b": 100, "t": "ref", "i": "test ore", "n": 1}
      ]}
    },
    {
      "name": "uninitialised account",
      "move": {"s": [
        {"b": 100, "t": "ref", "i": "test ore", "n": 3}
      ]}
    }
  ])");

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 100);
}

TEST_F (ServicesMoveTests, ServicesAfterCoinOperations)
{
  Process (R"([
    {
      "name": "domob",
      "move":
        {
          "s": [{"b": 100, "t": "ref", "i": "test ore", "n": 3}],
          "vc": {"b": 100}
        }
    }
  ])");

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 0);
  auto i = inv.Get (100, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("test ore"), 3);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("bar"), 0);
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("zerospace"), 0);
}

TEST_F (ServicesMoveTests, ServicesAfterCharacterUpdates)
{
  db.SetNextId (200);
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 200);
  c->SetBuildingId (100);
  c->MutableRegenData ().mutable_max_hp ()->set_armour (100);
  c->MutableHP ().set_armour (5);
  c.reset ();

  Process (R"([
    {
      "name": "domob",
      "move":
        {
          "s": [{"b": 100, "t": "fix", "c": 200}],
          "c": {"id": 200, "xb": {}}
        }
    }
  ])");

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 100);
  c = characters.GetById (200);
  EXPECT_FALSE (c->IsInBuilding ());
  EXPECT_EQ (c->GetHP ().armour (), 5);
  EXPECT_FALSE (c->IsBusy ());
}

/* ************************************************************************** */

class DexMoveTests : public MoveProcessorTests
{

protected:

  BuildingsTable buildings;
  BuildingInventoriesTable inv;
  CharacterTable characters;
  DexOrderTable orders;

  DexMoveTests ()
    : buildings(db), inv(db), characters(db), orders(db)
  {
    db.SetNextId (100);
    buildings.CreateNew ("checkmark", "", Faction::ANCIENT);
    inv.Get (100, "domob")->GetInventory ().AddFungibleCount ("foo", 100);
  }

  /**
   * Returns the balance of the given account for "foo" inside
   * our test building.
   */
  Quantity
  GetFooBalance (const std::string& account)
  {
    return inv.Get (100, account)->GetInventory ().GetFungibleCount ("foo");
  }

};

TEST_F (DexMoveTests, Works)
{
  Process (R"([
    {
      "name": "domob",
      "move": {"x": [
        {"x": "invalid"},
        {"b": 100, "i": "foo", "n": 10, "t": "andy"},
        {"b": 100, "i": "foo", "n": 100, "t": "daniel"},
        {"b": 100, "i": "foo", "n": 20, "t": "daniel"}
      ]}
    }
  ])");

  EXPECT_EQ (GetFooBalance ("domob"), 70);
  EXPECT_EQ (GetFooBalance ("andy"), 10);
  EXPECT_EQ (GetFooBalance ("daniel"), 20);
}

TEST_F (DexMoveTests, AfterCoinOperations)
{
  accounts.CreateNew ("domob")->AddBalance (100);

  db.SetNextId (101);
  Process (R"([
    {
      "name": "domob",
      "move":
        {
          "x": [{"b": 100, "i": "foo", "n": 6, "bp": 10}],
          "vc": {"t": {"andy": 60}}
        }
    }
  ])");

  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 40);
  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 60);

  EXPECT_EQ (orders.GetById (101), nullptr);
  EXPECT_EQ (db.GetNextId (), 101);
}

TEST_F (DexMoveTests, BeforeCharacterUpdates)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  db.SetNextId (200);
  auto c = characters.CreateNew ("domob", Faction::RED);
  CHECK_EQ (c->GetId (), 200);
  c->SetBuildingId (100);
  c->MutableProto ().set_cargo_space (1'000);
  c.reset ();

  Process (R"([
    {
      "name": "domob",
      "move": {
        "c": {"id": 200, "pu": {"f": {"foo": 999}}},
        "x": [{"b": 100, "i": "foo", "n": 10, "t": "andy"}]
      }
    }
  ])");

  EXPECT_EQ (GetFooBalance ("domob"), 0);
  EXPECT_EQ (GetFooBalance ("andy"), 10);

  c = characters.GetById (200);
  EXPECT_EQ (c->GetInventory ().GetFungibleCount ("foo"), 90);
}

/* ************************************************************************** */

class GodModeTests : public MoveProcessorTests
{

protected:

  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;
  CharacterTable tbl;
  GroundLootTable loot;

  GodModeTests ()
    : buildings(db), buildingInv(db), tbl(db), loot(db)
  {}

};

TEST_F (GodModeTests, InvalidTeleport)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  const auto id = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  ASSERT_EQ (id, 1);

  ProcessAdmin (R"([{"cmd": {
    "god": false
  }}])");
  ProcessAdmin (R"([{"cmd": {
    "god": {"teleport": [{"id": 1, "pos": {"x": 5, "y": 0, "z": 42}}]}
  }}])");

  EXPECT_EQ (tbl.GetById (id)->GetPosition (), HexCoord (0, 0));
}

TEST_F (GodModeTests, Teleport)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
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
          [
            {"id": 2, "pos": {"x": 0, "y": 0}},
            {"id": 1, "pos": {"x": 5, "y": -42}}
          ]
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
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  ASSERT_EQ (id, 1);
  c->MutableHP ().set_armour (50);
  c->MutableHP ().set_shield (20);
  auto& regen = c->MutableRegenData ();
  regen.mutable_max_hp ()->set_armour (50);
  regen.mutable_max_hp ()->set_shield (20);
  c.reset ();

  auto b = buildings.CreateNew ("checkmark", "domob", Faction::RED);
  ASSERT_EQ (b->GetId (), 2);
  b->MutableHP ().set_armour (50);
  b->MutableHP ().set_shield (20);
  b.reset ();

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "sethp":
          {
            "b":
              [
                {"id": 2, "ma": 200, "a": 5}
              ],
            "c":
              [
                {"id": 2, "a": 5},
                {"id": 1, "a": 32, "s": 15, "ma": -5, "ms": false, "x": "y"},
                {"id": 1, "a": 1.0, "s": 2e2, "ma": 5.0, "ms": 3e1}
              ]
          }
      }
  }}])");

  c = tbl.GetById (id);
  EXPECT_EQ (c->GetHP ().armour (), 32);
  EXPECT_EQ (c->GetHP ().shield (), 15);
  EXPECT_EQ (c->GetRegenData ().max_hp ().armour (), 50);
  EXPECT_EQ (c->GetRegenData ().max_hp ().shield (), 20);
  c.reset ();

  b = buildings.GetById (2);
  EXPECT_EQ (b->GetHP ().armour (), 5);
  EXPECT_EQ (b->GetHP ().shield (), 20);
  EXPECT_EQ (b->GetRegenData ().max_hp ().armour (), 200);
  b.reset ();

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "sethp":
          {
            "c":
              [
                {"id": 1, "a": 1.5, "s": -15, "ma": 100, "ms": 90}
              ]
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

TEST_F (GodModeTests, Build)
{
  ctx.SetHeight (10);
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);

  /* This is entirely invalid (not even an array).  */
  ProcessAdmin (R"([{"cmd": {"god": {"build": {"foo": "bar"}}}}])");

  /* All are invalid except for the last two.  They should create a building
     for domob and one ancient one.  */
  ProcessAdmin (R"([{"cmd": {"god": {"build": [
    42,
    null,
    {"t": "checkmark", "o": "domob", "c": {"x": 1, "y": 2}, "rot": 0, "x": 42},
    {"t": "checkmark", "c": {"x": 1, "y": 2}, "rot": 0},

    {"t": 42, "o": "domob", "c": {"x": 1, "y": 2}, "rot": 0},
    {"t": "invalid", "o": "domob", "c": {"x": 1, "y": 2}, "rot": 0},

    {"t": "checkmark", "o": 42, "c": {"x": 1, "y": 2}, "rot": 0},
    {"t": "checkmark", "o": "invalid", "c": {"x": 1, "y": 2}, "rot": 0},

    {"t": "checkmark", "o": "domob", "c": {"x": 1, "y": 2}, "rot": -1},
    {"t": "checkmark", "o": "domob", "c": {"x": 1, "y": 2}, "rot": 6},
    {"t": "checkmark", "o": "domob", "c": {"x": 1, "y": 2}, "rot": 1.5},

    {"t": "checkmark", "o": "domob", "c": {"x": 1.5, "y": 2}, "rot": 0},

    {"o": "domob", "c": {"x": 1, "y": 2}, "rot": 0, "x": 1},
    {"t": "checkmark", "c": {"x": 1, "y": 2}, "rot": 0, "x": 1},
    {"t": "checkmark", "o": "domob", "rot": 0, "x": 1},
    {"t": "checkmark", "o": "domob", "c": {"x": 1, "y": 2}, "x": 1},

    {"t": "checkmark", "o": "domob", "c": {"x": -100, "y": -200}, "rot": 0},
    {"t": "checkmark", "o": null, "c": {"x": 100, "y": 200}, "rot": 5}
  ]}}}])");

  auto res = buildings.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto h = buildings.GetFromResult (res);
  EXPECT_EQ (h->GetId (), 1);
  EXPECT_EQ (h->GetType (), "checkmark");
  EXPECT_EQ (h->GetFaction (), Faction::RED);
  EXPECT_EQ (h->GetOwner (), "domob");
  EXPECT_EQ (h->GetCentre (), HexCoord (-100, -200));
  EXPECT_EQ (h->GetProto ().shape_trafo ().rotation_steps (), 0);
  EXPECT_EQ (h->GetProto ().age_data ().founded_height (), 10);
  EXPECT_EQ (h->GetProto ().age_data ().finished_height (), 10);

  ASSERT_TRUE (res.Step ());
  h = buildings.GetFromResult (res);
  EXPECT_EQ (h->GetId (), 2);
  EXPECT_EQ (h->GetType (), "checkmark");
  EXPECT_EQ (h->GetFaction (), Faction::ANCIENT);
  EXPECT_EQ (h->GetCentre (), HexCoord (100, 200));
  EXPECT_EQ (h->GetProto ().shape_trafo ().rotation_steps (), 5);
  EXPECT_FALSE (res.Step ());
}

TEST_F (GodModeTests, InvalidDropLoot)
{
  const HexCoord pos(1, 2);
  db.SetNextId (100);
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);

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
              "building": {"id": 100, "a": "domob"},
              "fungible": {"foo": 10}
            },
            {
              "pos": {"x": 1, "y": 2},
              "fungible": {"foo": 10000000000000000}
            },
            {
              "fungible": {"foo": 10}
            },
            {
              "building": {"id": -5, "a": "domob"},
              "fungible": {"foo": 10}
            },
            {
              "building": {"id": 100, "a": false},
              "fungible": {"foo": 10}
            },
            {
              "building": {"id": 100, "a": "domob", "x": 5},
              "fungible": {"foo": 10}
            }
          ]
      }
  }}])");

  EXPECT_TRUE (loot.GetByCoord (pos)->GetInventory ().IsEmpty ());
  EXPECT_TRUE (buildingInv.Get (100, "domob")->GetInventory ().IsEmpty ());
}

TEST_F (GodModeTests, ValidDropLoot)
{
  const HexCoord pos(1, 2);
  db.SetNextId (100);
  buildings.CreateNew ("checkmark", "", Faction::ANCIENT);

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
              "fungible": {"foo": 10, "bar": 1000000}
            },
            {
              "building": {"id": 100, "a": "domob"},
              "fungible": {"foo": 22}
            },
            {
              "building": {"id": 100, "a": "domob"},
              "fungible": {"foo": 20}
            }
          ]
      }
  }}])");

  auto h = loot.GetByCoord (pos);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 20);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), 1'000'000);
  auto i = buildingInv.Get (100, "domob");
  EXPECT_EQ (i->GetInventory ().GetFungibleCount ("foo"), 42);
  EXPECT_NE (accounts.GetByName ("domob"), nullptr);
}

TEST_F (GodModeTests, GiftCoins)
{
  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "x": "foo",
        "giftcoins": "invalid"
      }
  }}])");

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "x": "foo",
        "giftcoins":
          {
            "andy": 5.42,
            "domob": 10
          }
      }
  }}])");

  ProcessAdmin (R"([{"cmd": {
    "god":
      {
        "x": "foo",
        "giftcoins":
          {
            "andy": 5,
            "domob": 10
          }
      }
  }}])");

  EXPECT_EQ (accounts.GetByName ("andy")->GetBalance (), 5);
  EXPECT_EQ (accounts.GetByName ("domob")->GetBalance (), 20);

  MoneySupply ms(db);
  EXPECT_EQ (ms.Get ("gifted"), 25);
}

/**
 * Test fixture for god mode but set up on mainnet, so that god mode is
 * actually not allowed.
 */
class GodModeDisabledTests : public GodModeTests
{

protected:

  GodModeDisabledTests ()
  {
    ctx.SetChain (xaya::Chain::MAIN);
  }

};

TEST_F (GodModeDisabledTests, Teleport)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  const auto id = tbl.CreateNew ("domob", Faction::RED)->GetId ();
  ASSERT_EQ (id, 1);

  ProcessAdmin (R"([{"cmd": {
    "god": {"teleport": [{"id": 1, "pos": {"x": 5, "y": -42}}]}
  }}])");

  EXPECT_EQ (tbl.GetById (id)->GetPosition (), HexCoord (0, 0));
}

TEST_F (GodModeDisabledTests, SetHp)
{
  accounts.CreateNew ("domob")->SetFaction (Faction::RED);
  auto c = tbl.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  ASSERT_EQ (id, 1);
  c->MutableHP ().set_armour (50);
  c.reset ();

  ProcessAdmin (R"([{"cmd": {
    "god": {"sethp": {"c": [{"id": 1, "a": 10}]}}
  }}])");

  EXPECT_EQ (tbl.GetById (id)->GetHP ().armour (), 50);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
