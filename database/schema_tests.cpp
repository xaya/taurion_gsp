#include "schema.hpp"

#include "dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

using SchemaTests = DBTestFixture;

TEST_F (SchemaTests, Works)
{
  SetupDatabaseSchema (db.GetHandle ());
}

TEST_F (SchemaTests, TwiceIsOk)
{
  SetupDatabaseSchema (db.GetHandle ());
  SetupDatabaseSchema (db.GetHandle ());
}

} // anonymous namespace
} // namespace pxd
