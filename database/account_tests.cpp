#include "account.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class AccountTests : public DBTestWithSchema
{

protected:

  AccountsTable tbl;

  AccountTests ()
    : tbl(db)
  {}

};

TEST_F (AccountTests, DefaultData)
{
  auto a = tbl.GetByName ("foobar");
  EXPECT_EQ (a->GetName (), "foobar");
  EXPECT_EQ (a->GetKills (), 0);
  EXPECT_EQ (a->GetFame (), 100);
}

TEST_F (AccountTests, Update)
{
  auto a = tbl.GetByName ("foobar");
  a->SetKills (50);
  a->SetFame (200);
  a.reset ();

  a = tbl.GetByName ("foobar");
  EXPECT_EQ (a->GetName (), "foobar");
  EXPECT_EQ (a->GetKills (), 50);
  EXPECT_EQ (a->GetFame (), 200);
}

TEST_F (AccountTests, DefaultNotWritten)
{
  tbl.GetByName ("foobar");
  auto res = tbl.QueryNonTrivial ();
  EXPECT_FALSE (res.Step ());
}

using AccountsTableTests = AccountTests;

TEST_F (AccountsTableTests, QueryNonTrivial)
{
  tbl.GetByName ("foo");
  tbl.GetByName ("bar")->SetFame (10);
  tbl.GetByName ("baz")->SetKills (42);

  auto res = tbl.QueryNonTrivial ();

  ASSERT_TRUE (res.Step ());
  auto a = tbl.GetFromResult (res);
  EXPECT_EQ (a->GetName (), "bar");
  EXPECT_EQ (a->GetFame (), 10);

  ASSERT_TRUE (res.Step ());
  a = tbl.GetFromResult (res);
  EXPECT_EQ (a->GetName (), "baz");
  EXPECT_EQ (a->GetKills (), 42);

  ASSERT_FALSE (res.Step ());
}

} // anonymous namespace
} // namespace pxd
