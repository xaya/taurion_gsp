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
   * All targets that queries consider, bucketed into cells keyed by HexCoord
   * of the cell indices.
   */
  std::unordered_map<HexCoord, Cell> cells;

  /** The same targets, keyed by their position.  */
  std::unordered_map<HexCoord, std::vector<Target>> tiles;

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
    return coord >= 0 ? coord / CELL_SIZE
                      : -((CELL_SIZE - 1 - coord) / CELL_SIZE);
  }

  /**
   * Calls fcn with each target and its distance that matches a query as
   * done by FindInRange.  The order is unspecified, except that if
   * targets are processed in order of distance, then done is called after
   * each distance and the search stops if it returns true.
   */
  template <typename Fcn, typename Done>
    void ForEachInRange (const HexCoord& centre, HexCoord::IntT l1range,
                         Faction faction, bool enemies, bool friendlies,
                         const Fcn& fcn, const Done& done) const;

public:

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
   * Returns all targets in the given L1 range except for the one with ID
   * exclude.  This function can be used to query for enemies, friendlies
   * or all.  Targets are returned in the order of TargetKey, i.e. buildings
   * before characters and each by ID.
   *
   * If limit is nonzero, at most that many are returned:  those closest
   * to the centre, and of those at the same distance the first ones in
   * TargetKey order.
   */
  std::vector<proto::TargetId> FindInRange (const HexCoord& centre,
                                            HexCoord::IntT l1range,
                                            Faction faction,
                                            bool enemies, bool friendlies,
                                            const proto::TargetId& exclude,
                                            size_t limit = 0) const;

  /**
   * Returns those of the targets FindInRange would return that are
   * closest to the centre, in the same order.  This is faster than finding
   * them with FindInRange.
   */
  std::vector<proto::TargetId> FindClosest (const HexCoord& centre,
                                            HexCoord::IntT l1range,
                                            Faction faction,
                                            bool enemies, bool friendlies,
                                            const proto::TargetId& exclude)
      const;

  /**
   * Returns true if FindInRange with these arguments (and no limit) would
   * return the given target.  This only looks up that target, so it is a
   * fast check whether it is still in range and attackable.
   */
  bool IsInRange (const proto::TargetId& id, const HexCoord& centre,
                  HexCoord::IntT l1range, Faction faction,
                  bool enemies, bool friendlies) const;

  /**
   * Returns the position of a target, or null if it is not a potential target
   * (e.g. an ancient building or a character inside a building).
   */
  const HexCoord* FindPosition (const proto::TargetId& id) const;

};

} // namespace pxd

#endif // DATABASE_TARGET_HPP
