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

#ifndef PROTO_ROCONFIG_HPP
#define PROTO_ROCONFIG_HPP

#include "config.pb.h"

namespace pxd
{

/**
 * Returns the singleton, read-only instance of the global ConfigData proto.
 */
const proto::ConfigData& RoConfigData ();

} // namespace pxd

#endif // PROTO_ROCONFIG_HPP
