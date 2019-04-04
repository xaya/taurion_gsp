#include "prizes.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class PrizesTests : public DBTestWithSchema
{

protected:

  /** Prizes instance for tests.  */
  Prizes p;

  PrizesTests ()
    : p(db)
  {
    auto stmt = db.Prepare (R"(
      INSERT INTO `prizes` (`name`, `found`) VALUES (?1, ?2)
    )");

    stmt.Bind<std::string> (1, "gold");
    stmt.Bind (2, 10);
    stmt.Execute ();

    stmt.Reset ();
    stmt.Bind<std::string> (1, "silver");
    stmt.Bind (2, 0);
    stmt.Execute ();
  }

};

TEST_F (PrizesTests, GetFound)
{
  EXPECT_EQ (p.GetFound ("gold"), 10);
  EXPECT_EQ (p.GetFound ("silver"), 0);
}

TEST_F (PrizesTests, IncrementFound)
{
  p.IncrementFound ("gold");
  EXPECT_EQ (p.GetFound ("gold"), 11);
  EXPECT_EQ (p.GetFound ("silver"), 0);

  p.IncrementFound ("silver");
  EXPECT_EQ (p.GetFound ("gold"), 11);
  EXPECT_EQ (p.GetFound ("silver"), 1);
}

} // anonymous namespace
} // namespace pxd
