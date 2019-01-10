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
  auto res = tbl.GetAll ();
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
  auto res = tbl.GetAll ();
  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    EXPECT_EQ (c.GetOwner (), "domob");
    EXPECT_EQ (c.GetName (), "foo");
  }
  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    EXPECT_EQ (c.GetOwner (), "domob");
    EXPECT_EQ (c.GetName (), "bar");
  }
  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    EXPECT_EQ (c.GetOwner (), "andy");
    EXPECT_EQ (c.GetName (), "baz");
  }
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
  auto res = tbl.GetAll ();
  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    EXPECT_EQ (c.GetOwner (), "domob");
    EXPECT_EQ (c.GetName (), "baz");
  }
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
  auto res = tbl.GetAll ();
  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    EXPECT_EQ (c.GetOwner (), "domob");
    EXPECT_EQ (c.GetName (), "foo");
  }
  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    EXPECT_EQ (c.GetOwner (), "domob");
    EXPECT_EQ (c.GetName (), "bar");
  }
  EXPECT_FALSE (res.Step ());
}

} // anonymous namespace
} // namespace pxd
