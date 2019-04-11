#ifndef PXD_FAME_TESTS_HPP
#define PXD_FAME_TESTS_HPP

#include "fame.hpp"

#include "database/damagelists.hpp"
#include "database/database.hpp"

#include <gmock/gmock.h>

namespace pxd
{

/**
 * Mock instance of the FameUpdater, which can be used to make sure in tests
 * that the update function is called correctly.
 */
class MockFameUpdater : public FameUpdater
{

public:

  explicit MockFameUpdater (Database& db, unsigned height);

  MOCK_METHOD2 (UpdateForKill, void (Database::IdT victim,
                                     const DamageLists::Attackers& attackers));

  using FameUpdater::UpdateForKill;

};

} // namespace pxd

#endif // PXD_FAME_TESTS_HPP
