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

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace pxd
{

/**
 * Abstraction to give access to "targets" in the database.  They are either
 * characters or buildings, from their respective tables.  This class allows
 * querying both, and handles finding potential in-range and enemy entities.
 *
 * The positions and factions of all targets are read from the database once,
 * when the instance is constructed, and queries are answered from memory.
 * An instance must thus only be used while no target is added, removed,
 * moved or changes faction (e.g. within one phase of combat processing);
 * changes to all other data (like HP) are fine.
 */
class TargetFinder
{

private:

  /** Data about one target.  */
  struct Target
  {
    HexCoord pos;
    Faction faction;
    proto::TargetId::Type type;
    Database::IdT id;
  };

  /** Side length of the square cells that targets are bucketed into.  */
  static constexpr int CELL_SIZE = 16;

  /** All targets, bucketed into cells keyed by CellKey.  */
  std::unordered_map<uint32_t, std::vector<Target>> cells;

  /**
   * Returns the cell index (along one axis) of a coordinate.
   */
  static int
  CellIndex (const int coord)
  {
    return coord >= 0 ? coord / CELL_SIZE : -((CELL_SIZE - 1 - coord) / CELL_SIZE);
  }

  /**
   * Returns the key in cells for the cell with given indices.
   */
  static uint32_t
  CellKey (const int x, const int y)
  {
    return (static_cast<uint32_t> (static_cast<uint16_t> (x)) << 16)
              | static_cast<uint16_t> (y);
  }

public:

  /** Type for a callback function that processes targets.  */
  using ProcessingFcn
      = std::function<void (const HexCoord&, const proto::TargetId&)>;

  explicit TargetFinder (Database& db);

  TargetFinder () = delete;
  TargetFinder (const TargetFinder&) = delete;
  void operator= (const TargetFinder&) = delete;

  /**
   * Finds all targets in the given L1 range and executes the
   * callback on each of the resulting Target instances.  This function can
   * be used to query for enemies, friendlies or all.  Targets are processed
   * in the order of TargetKey, i.e. buildings before characters and each
   * by ID.
   */
  void ProcessL1Targets (const HexCoord& centre, HexCoord::IntT l1range,
                         Faction faction, bool enemies, bool friendlies,
                         const ProcessingFcn& cb) const;

};

} // namespace pxd

#endif // DATABASE_TARGET_HPP
