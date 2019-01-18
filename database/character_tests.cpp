#include "character.hpp"

#include "dbtest.hpp"

#include "proto/character.pb.h"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class CharacterTests : public DBTestWithSchema
{

protected:

  /** CharacterTable instance for tests.  */
  CharacterTable tbl;

  CharacterTests ()
    : tbl(*db)
  {}

};

TEST_F (CharacterTests, Creation)
{
  const HexCoord pos(5, -2);

  auto c = tbl.CreateNew  ("domob", "abc");
  c->SetPosition (pos);
  const auto id1 = c->GetId ();
  c.reset ();

  c = tbl.CreateNew ("domob", u8"äöü");
  const auto id2 = c->GetId ();
  c->MutableProto ().mutable_movement ();
  c.reset ();

  auto res = tbl.QueryAll ();

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  ASSERT_EQ (c->GetId (), id1);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetName (), "abc");
  EXPECT_EQ (c->GetPosition (), pos);
  EXPECT_FALSE (c->GetProto ().has_movement ());

  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  ASSERT_EQ (c->GetId (), id2);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetName (), u8"äöü");
  EXPECT_TRUE (c->GetProto ().has_movement ());

  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTests, ModificationWithProto)
{
  const HexCoord pos1(5, -2);
  const HexCoord pos2(-2, 5);

  auto c = tbl.CreateNew ("domob", "foo");
  c->SetPosition (pos1);
  c.reset ();

  auto res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "domob");
  EXPECT_EQ (c->GetPosition (), pos1);
  EXPECT_FALSE (c->GetProto ().has_movement ());
  ASSERT_FALSE (res.Step ());

  c->SetOwner ("andy");
  c->SetPosition (pos2);
  c->MutableProto ().mutable_movement ();
  c.reset ();

  res = tbl.QueryAll ();
  ASSERT_TRUE (res.Step ());
  c = tbl.GetFromResult (res);
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetPosition (), pos2);
  EXPECT_TRUE (c->GetProto ().has_movement ());
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTests, ModificationFieldsOnly)
{
  const HexCoord pos(-2, 5);

  tbl.CreateNew ("domob", "foo");

  auto c = tbl.GetById (1);
  ASSERT_TRUE (c != nullptr);
  c->SetOwner ("andy");
  c->SetPosition (pos);
  c.reset ();

  c = tbl.GetById (1);
  ASSERT_TRUE (c != nullptr);
  EXPECT_EQ (c->GetName (), "foo");
  EXPECT_EQ (c->GetOwner (), "andy");
  EXPECT_EQ (c->GetPosition (), pos);
}

TEST_F (CharacterTests, EmptyNameNotAllowed)
{
  EXPECT_DEATH ({
    tbl.CreateNew ("domob", "");
  }, "name");
}

using CharacterTableTests = CharacterTests;

TEST_F (CharacterTableTests, GetById)
{
  const auto id1 = tbl.CreateNew ("domob", "abc")->GetId ();
  const auto id2 = tbl.CreateNew ("domob", "foo")->GetId ();

  CHECK (tbl.GetById (500) == nullptr);
  CHECK_EQ (tbl.GetById (id1)->GetName (), "abc");
  CHECK_EQ (tbl.GetById (id2)->GetName (), "foo");
}

TEST_F (CharacterTableTests, QueryForOwner)
{
  tbl.CreateNew ("domob", "abc");
  tbl.CreateNew ("domob", "foo");
  tbl.CreateNew ("andy", "test");

  auto res = tbl.QueryForOwner ("domob");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetName (), "abc");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetName (), "foo");
  ASSERT_FALSE (res.Step ());

  res = tbl.QueryForOwner ("not there");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, IsValidName)
{
  tbl.CreateNew ("domob", "abc");

  EXPECT_FALSE (tbl.IsValidName (""));
  EXPECT_FALSE (tbl.IsValidName ("abc"));
  EXPECT_TRUE (tbl.IsValidName ("foo"));
}

} // anonymous namespace
} // namespace pxd
