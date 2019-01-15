#include "moveprocessor.hpp"

#include "jsonutils.hpp"
#include "params.hpp"

#include "database/character.hpp"

#include "database/dbtest.hpp"

#include <gtest/gtest.h>

#include <json/json.h>

#include <sstream>
#include <string>

namespace pxd
{
namespace
{

#define DEVADDR "DHy2615XKevE23LVRVZVxGeqxadRGyiFW4"

class MoveProcessorTests : public DBTestWithSchema
{

protected:

  /** Params instance that is used.  Set to mainnet.  */
  const Params params;

private:

  /** MoveProcessor instance for use in the test.  */
  MoveProcessor mvProc;

protected:

  MoveProcessorTests ()
    : params(xaya::Chain::MAIN), mvProc(*db, params)
  {}

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
      entry["out"][DEVADDR] = AmountToJson (amount);

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
    "out": {")" DEVADDR R"(": false}
  }])"), "JSON value for amount is not double");
}

/* ************************************************************************** */

using CharacterCreationTests = MoveProcessorTests;

TEST_F (CharacterCreationTests, InvalidCommands)
{
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {}},
    {"name": "domob", "move": {"nc": 42}},
    {"name": "domob", "move": {"nc": {}}},
    {"name": "domob", "move": {"nc": {"name": "foo", "other": false}}}
  ])", params.CharacterCost ());

  CharacterTable tbl(*db);
  auto res = tbl.QueryAll ();
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, ValidCreation)
{
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": {"name": "foo"}}},
    {"name": "domob", "move": {"nc": {"name": "bar"}}},
    {"name": "andy", "move": {"nc": {"name": "baz"}}}
  ])", params.CharacterCost ());

  CharacterTable tbl(*db);
  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetName (), "foo");
  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetName (), "bar");
  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetName (), "baz");
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, DevPayment)
{
  Process (R"([{"name": "domob", "move": {"nc": {"name": "foo"}}}])");
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": {"name": "bar"}}}
  ])", params.CharacterCost () - 1);
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": {"name": "baz"}}}
  ])", params.CharacterCost () + 1);

  CharacterTable tbl(*db);
  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetName (), "baz");
  EXPECT_FALSE (res.Step ());
}

TEST_F (CharacterCreationTests, NameValidation)
{
  ProcessWithDevPayment (R"([
    {"name": "domob", "move": {"nc": {"name": ""}}},
    {"name": "domob", "move": {"nc": {"name": "foo"}}},
    {"name": "domob", "move": {"nc": {"name": "bar"}}},
    {"name": "andy", "move": {"nc": {"name": "foo"}}}
  ])", params.CharacterCost ());

  CharacterTable tbl(*db);
  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetName (), "foo");
  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetName (), "bar");
  EXPECT_FALSE (res.Step ());
}

/* ************************************************************************** */

class CharacterUpdateTests : public MoveProcessorTests
{

protected:

  /** Character table to be used in tests.  */
  CharacterTable tbl;

  /**
   * Retrieves a handle to the test character.
   */
  CharacterTable::Handle
  GetTest ()
  {
    auto h = tbl.GetById (1);
    CHECK (h != nullptr);
    CHECK_EQ (h->GetName (), "test");
    return h;
  }

  /**
   * Inserts a new character with the given ID, name and owner.
   */
  void
  SetupCharacter (const unsigned id, const std::string& owner,
                  const std::string& name)
  {
    SetNextId (id);
    tbl.CreateNew (owner, name);

    auto h = tbl.GetById (id);
    CHECK (h != nullptr);
    CHECK_EQ (h->GetName (), name);
    CHECK_EQ (h->GetOwner (), owner);
  }

  /**
   * All CharacterUpdateTests will start with a test character already created.
   * We also ensure that it has the ID 1.
   */
  CharacterUpdateTests ()
    : tbl(*db)
  {
    SetupCharacter (1, "domob", "test");
  }

};

TEST_F (CharacterUpdateTests, CreationAndUpdate)
{
  ProcessWithDevPayment (R"([{
    "name": "domob",
    "move":
      {
        "nc": {"name": "foo"},
        "c": {"1": {"send": "daniel"}, "2": {"send": "andy"}}
      }
  }])", params.CharacterCost ());

  /* Transfer and creation should work fine together for two different
     characters (but in the same move).  */
  EXPECT_EQ (GetTest ()->GetOwner (), "daniel");

  /* The character created in the same move should not be transferred.  */
  auto h = tbl.GetById (2);
  ASSERT_TRUE (h != nullptr);
  EXPECT_EQ (h->GetName (), "foo");
  EXPECT_EQ (h->GetOwner (), "domob");
}

TEST_F (CharacterUpdateTests, ValidTransfer)
{
  EXPECT_EQ (GetTest ()->GetOwner (), "domob");
  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"send": "andy"}}}
  }])");
  EXPECT_EQ (GetTest ()->GetOwner (), "andy");
}

TEST_F (CharacterUpdateTests, InvalidTransfer)
{
  Process (R"([{
    "name": "domob",
    "move": {"c": {"1": {"send": false}}}
  }])");
  EXPECT_EQ (GetTest ()->GetOwner (), "domob");
}

TEST_F (CharacterUpdateTests, OwnerCheck)
{
  /* Verify that a later update works fine even if a previous character update
     (from the same move) failed due to the owner check.  */
  SetupCharacter (9, "andy", "later");

  EXPECT_EQ (GetTest ()->GetOwner (), "domob");
  EXPECT_EQ (tbl.GetById (9)->GetOwner (), "andy");
  Process (R"([{
    "name": "andy",
    "move": {"c": {"1": {"send": "andy"}, "9": {"send": "domob"}}}
  }])");
  EXPECT_EQ (GetTest ()->GetOwner (), "domob");
  EXPECT_EQ (tbl.GetById (9)->GetOwner (), "domob");
}

TEST_F (CharacterUpdateTests, InvalidUpdate)
{
  /* We want to test that one invalid update still allows for other
     updates (i.e. other characters) to be done successfully in the same
     move transaction.  Thus create another character with a later ID that
     we will use for successful updates.  */
  SetupCharacter (9, "domob", "later");

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

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
