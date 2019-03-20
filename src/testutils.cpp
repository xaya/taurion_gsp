#include "testutils.hpp"

#include <xayagame/hash.hpp>

namespace pxd
{

TestRandom::TestRandom ()
{
  xaya::SHA256 seed;
  seed << "test seed";
  Seed (seed.Finalise ());
}

} // namespace pxd
