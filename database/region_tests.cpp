#include "region.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class RegionTests : public DBTestWithSchema
{

protected:

  /** RegionsTable instance for tests.  */
  RegionsTable tbl;

  RegionTests ()
    : tbl(db)
  {}

};

TEST_F (RegionTests, DefaultData)
{
  auto r = tbl.GetById (42);
  EXPECT_EQ (r->GetId (), 42);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (RegionTests, Update)
{
  tbl.GetById (42)->MutableProto ().set_prospecting_character (100);

  auto r = tbl.GetById (42);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 100);

  r = tbl.GetById (100);
  EXPECT_EQ (r->GetId (), 100);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (RegionTests, IdZero)
{
  tbl.GetById (0)->MutableProto ().set_prospecting_character (100);

  auto r = tbl.GetById (0);
  EXPECT_EQ (r->GetId (), 0);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 100);
}

TEST_F (RegionTests, DefaultNotWritten)
{
  tbl.GetById (42);
  auto res = tbl.QueryNonTrivial ();
  EXPECT_FALSE (res.Step ());
}

using RegionsTableTests = RegionTests;

TEST_F (RegionsTableTests, QueryNonTrivial)
{
  tbl.GetById (1);
  tbl.GetById (3)->MutableProto ();
  tbl.GetById (0)->MutableProto ();
  tbl.GetById (2);

  auto res = tbl.QueryNonTrivial ();

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 0);

  ASSERT_TRUE (res.Step ());
  EXPECT_EQ (tbl.GetFromResult (res)->GetId (), 3);

  ASSERT_FALSE (res.Step ());
}

} // anonymous namespace
} // namespace pxd
