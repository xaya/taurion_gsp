#ifndef PXD_TESTUTILS_HPP
#define PXD_TESTUTILS_HPP

#include <xayautil/random.hpp>

namespace pxd
{

/**
 * Random instance that seeds itself on construction from a fixed test seed.
 */
class TestRandom : public xaya::Random
{

public:

  TestRandom ();

};

} // namespace pxd

#endif // PXD_TESTUTILS_HPP
