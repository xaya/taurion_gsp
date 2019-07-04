/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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
