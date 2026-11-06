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

#include <array>
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

  /** Number of Faction values, for the counts per faction.  */
  static constexpr unsigned NUM_FACTIONS = 5;

  /** Side length of the square cells that targets are bucketed into.  */
  static constexpr int CELL_SIZE = 8;

  /** The targets in one cell.  */
  struct Cell
  {
    std::vector<Target> targets;

    /** Number of the targets per faction.  */
    std::array<unsigned, NUM_FACTIONS> counts = {};
  };

  /**
   * All targets that queries consider, bucketed into cells keyed by CoordKey
   * of the cell indices.
   */
  std::unordered_map<uint32_t, Cell> cells;

  /** The same targets, keyed by CoordKey of their position.  */
  std::unordered_map<uint32_t, std::vector<Target>> tiles;

  /** Positions of all targets, keyed by PositionKey.  */
  std::unordered_map<uint64_t, HexCoord> positions;

  /**
   * Returns the key in positions for a target.
   */
  static uint64_t
  PositionKey (const proto::TargetId::Type type, const Database::IdT id)
  {
    return (id << 2) | type;
  }

  /**
   * Returns the cell index (along one axis) of a coordinate.
   */
  static int
  CellIndex (const int coord)
  {
    return coord >= 0 ? coord / CELL_SIZE : -((CELL_SIZE - 1 - coord) / CELL_SIZE);
  }

  /**
   * Returns the key in cells or tiles for the given x and y.
   */
  static uint32_t
  CoordKey (const int x, const int y)
  {
    return (static_cast<uint32_t> (static_cast<uint16_t> (x)) << 16)
              | static_cast<uint16_t> (y);
  }

  /**
   * Calls fcn with each target and its distance that matches a query as
   * done by ProcessL1Targets.  The order is unspecified, except that if
   * targets are processed in order of distance, then done is called after
   * each distance and the search stops if it returns true.
   */
  template <typename Fcn, typename Done>
    void ForEachInRange (const HexCoord& centre, HexCoord::IntT l1range,
                         Faction faction, bool enemies, bool friendlies,
                         const Fcn& fcn, const Done& done) const;

public:

  /** Type for a callback function that processes targets.  */
  using ProcessingFcn
      = std::function<void (const HexCoord&, const proto::TargetId&)>;

  /** Type for a predicate on positions.  */
  using PositionFilter = std::function<bool (const HexCoord&)>;

  /**
   * Reads all targets from the database.  Queries ignore targets at
   * positions for which the ignore predicate (if given) is true.
   */
  explicit TargetFinder (Database& db, const PositionFilter& ignore = nullptr);

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

  /**
   * Returns those of the targets ProcessL1Targets would process that are
   * closest to the centre, except for the one with ID exclude, in the same
   * order.  This is faster than finding them with ProcessL1Targets.
   */
  std::vector<proto::TargetId> FindClosest (const HexCoord& centre,
                                            HexCoord::IntT l1range,
                                            Faction faction,
                                            bool enemies, bool friendlies,
                                            const proto::TargetId& exclude)
      const;

  /**
   * Returns the position of a target.  It must be a potential target
   * (e.g. not an ancient building or a character inside a building).
   */
  const HexCoord& GetPosition (const proto::TargetId& id) const;

};

} // namespace pxd

#endif // DATABASE_TARGET_HPP
