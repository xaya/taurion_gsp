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
  SetupDatabaseSchema (handle);
}

TEST_F (SchemaTests, TwiceIsOk)
{
  SetupDatabaseSchema (handle);
  SetupDatabaseSchema (handle);
}

} // anonymous namespace
} // namespace pxd
