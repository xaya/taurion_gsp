#include "character.hpp"

#include "dbtest.hpp"

#include "proto/character.pb.h"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using CharacterTests = DBTestWithSchema;

TEST_F (CharacterTests, Creation)
{
  const HexCoord pos(5, -2);

  unsigned id1, id2;
  {
    Character c1(*db, "domob", "abc");
    c1.SetPosition (pos);
    id1 = c1.GetId ();
  }

  {
    Character c2(*db, "domob", u8"äöü");
    id2 = c2.GetId ();
    c2.MutableProto ().mutable_movement ();
  }

  CharacterTable tbl(*db);
  auto res = tbl.GetAll ();

  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    ASSERT_EQ (c.GetId (), id1);
    EXPECT_EQ (c.GetOwner (), "domob");
    EXPECT_EQ (c.GetName (), "abc");
    EXPECT_EQ (c.GetPosition (), pos);
    EXPECT_FALSE (c.GetProto ().has_movement ());
  }

  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    ASSERT_EQ (c.GetId (), id2);
    EXPECT_EQ (c.GetOwner (), "domob");
    EXPECT_EQ (c.GetName (), u8"äöü");
    EXPECT_TRUE (c.GetProto ().has_movement ());
  }

  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTests, Modification)
{
  const HexCoord pos1(5, -2);
  const HexCoord pos2(-2, 5);

  CharacterTable tbl(*db);

  {
    Character c(*db, "domob", "foo");
    c.SetPosition (pos1);
  }

  auto res = tbl.GetAll ();
  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    EXPECT_EQ (c.GetOwner (), "domob");
    EXPECT_EQ (c.GetPosition (), pos1);
    EXPECT_FALSE (c.GetProto ().has_movement ());

    c.SetOwner ("andy");
    c.SetPosition (pos2);
    c.MutableProto ().mutable_movement ();
  }
  ASSERT_FALSE (res.Step ());

  res = tbl.GetAll ();
  ASSERT_TRUE (res.Step ());
  {
    Character c(res);
    EXPECT_EQ (c.GetOwner (), "andy");
    EXPECT_EQ (c.GetPosition (), pos2);
    EXPECT_TRUE (c.GetProto ().has_movement ());
  }
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTests, EmptyNameNotAllowed)
{
  EXPECT_DEATH ({
    Character (*db, "domob", "");
  }, "name");
}

using CharacterTableTests = DBTestWithSchema;

TEST_F (CharacterTableTests, GetForOwner)
{
  Character (*db, "domob", "abc");
  Character (*db, "domob", "foo");
  Character (*db, "andy", "test");

  CharacterTable tbl(*db);

  auto res = tbl.GetForOwner ("domob");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (Character (res).GetName (), "abc");
  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (Character (res).GetName (), "foo");
  ASSERT_FALSE (res.Step ());

  res = tbl.GetForOwner ("not there");
  ASSERT_FALSE (res.Step ());
}

TEST_F (CharacterTableTests, IsValidName)
{
  Character (*db, "domob", "abc");

  CharacterTable tbl(*db);
  EXPECT_FALSE (tbl.IsValidName (""));
  EXPECT_FALSE (tbl.IsValidName ("abc"));
  EXPECT_TRUE (tbl.IsValidName ("foo"));
}

} // anonymous namespace
} // namespace pxd
