/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#ifndef DATABASE_TARGET_HPP
#define DATABASE_TARGET_HPP

#include "database.hpp"
#include "faction.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <functional>

namespace pxd
{

/**
 * Abstraction to give access to "targets" in the database.  They are either
 * characters or buildings, from their respective tables.  This class allows
 * querying both, and handles finding potential in-range and enemy entities.
 */
class TargetFinder
{

private:

  /** The Database reference for doing queries.  */
  Database& db;

public:

  /** Type for a callback function that processes targets.  */
  using ProcessingFcn
      = std::function<void (const HexCoord&, const proto::TargetId&)>;

  explicit TargetFinder (Database& d)
    : db(d)
  {}

  TargetFinder () = delete;
  TargetFinder (const TargetFinder&) = delete;
  void operator= (const TargetFinder&) = delete;

  /**
   * Finds all targets in the given L1 range and executes the
   * callback on each of the resulting Target instances.  This function can
   * be used to query for enemies, friendlies or all.
   */
  void ProcessL1Targets (const HexCoord& centre, HexCoord::IntT l1range,
                         Faction faction, bool enemies, bool friendlies,
                         const ProcessingFcn& cb) const;

};

} // namespace pxd

#endif // DATABASE_TARGET_HPP
