#include "spawn.hpp"

#include "database/dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class SpawnTests : public DBTestWithSchema
{

protected:

  /** Params instance for testing.  */
  const Params params;

  /** Character table for tests.  */
  CharacterTable tbl;

  SpawnTests ()
    : params(xaya::Chain::MAIN), tbl(db)
  {}

};

TEST_F (SpawnTests, Basic)
{
  SpawnCharacter ("domob", Faction::RED, tbl, params);
  SpawnCharacter ("domob", Faction::GREEN, tbl, params);
  SpawnCharacter ("andy", Faction::BLUE, tbl, params);

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  auto c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::RED);

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetFaction (), Faction::GREEN);

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetFaction (), Faction::BLUE);

  EXPECT_FALSE (res.Step ());
}

TEST_F (SpawnTests, DataInitialised)
{
  SpawnCharacter ("domob", Faction::RED, tbl, params);

  auto c = tbl.GetById (1);
  ASSERT_TRUE (c != nullptr);
  ASSERT_EQ (c->GetOwner (), "domob");

  EXPECT_TRUE (c->GetProto ().has_combat_data ());
  EXPECT_EQ (c->GetProto ().combat_data ().attacks_size (), 2);
}

} // anonymous namespace
} // namespace pxd
