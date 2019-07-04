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
