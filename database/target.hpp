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
 * An instance must thus only be used while no target is added, moved or
 * changes faction (e.g. within one phase of combat processing), and targets
 * that are destroyed have to be removed with Remove; changes to all other
 * data (like HP) are fine.
 */
class TargetFinder
{

private:

  /**
   * Data about one target.  Its key orders targets like TargetKey, i.e.
   * buildings before characters and each by ID.
   */
  struct Target
  {
    HexCoord pos;
    uint64_t key;
  };

  /** A range of targets in key order.  */
  struct Span
  {
    const Target* begin;
    const Target* end;
  };

  /** Number of Faction values.  */
  static constexpr unsigned NUM_FACTIONS = 5;

  /** Lists of targets (in key order) per faction.  */
  using ByFaction = std::array<std::vector<Target>, NUM_FACTIONS>;

  /** Side length of the square cells that targets are bucketed into.  */
  static constexpr int CELL_SIZE = 8;

  /**
   * All targets that queries consider, bucketed into cells keyed by HexCoord
   * of the cell indices.
   */
  std::unordered_map<HexCoord, ByFaction> cells;

  /** The same targets, keyed by their position.  */
  std::unordered_map<HexCoord, ByFaction> tiles;

  /** What is looked up directly about a target.  */
  struct Entry
  {
    HexCoord pos;
    Faction faction;
    /** Whether queries ignore the target (e.g. in a no-combat zone).  */
    bool ignored;
  };

  /** All targets (also those that queries ignore), by their key.  */
  std::unordered_map<uint64_t, Entry> entries;

  /** Number of bits of a target key for the ID.  */
  static constexpr unsigned ID_BITS = 56;

  /**
   * Returns the key of a target, whose ID must have at most ID_BITS bits.
   */
  static uint64_t Key (proto::TargetId::Type type, Database::IdT id);

  /**
   * Returns the ID of a target from its key.
   */
  static proto::TargetId IdFromKey (uint64_t key);

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
   * Calls fcn (spans) for the targets in range that match the faction
   * conditions (enemies or friendlies of the given faction), grouped by
   * distance in increasing order, until it returns true.  The targets at
   * one distance are given as spans, so that also a pile of them on one tile
   * is passed without looking at each.
   */
  template <typename Fcn>
    void ForEachInRange (const HexCoord& centre, HexCoord::IntT l1range,
                         Faction faction, bool enemies, bool friendlies,
                         const Fcn& fcn) const;

public:

  class Closest;

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
   * TargetKey order.  For this, only as many targets of each tile are
   * looked at as can be needed.
   */
  std::vector<proto::TargetId> FindInRange (const HexCoord& centre,
                                            HexCoord::IntT l1range,
                                            Faction faction,
                                            bool enemies, bool friendlies,
                                            const proto::TargetId& exclude,
                                            size_t limit = 0) const;

  /**
   * Returns those of the targets FindInRange would return that are
   * closest to the centre, in the same order.  They are only looked up
   * when accessed, so that also many of them (like a pile on one tile)
   * are cheap to find and pick from.
   */
  Closest FindClosest (const HexCoord& centre, HexCoord::IntT l1range,
                       Faction faction, bool enemies, bool friendlies,
                       const proto::TargetId& exclude) const;

  /**
   * Returns true if FindInRange with these arguments (excluding nothing and
   * without a limit) would return the given target.  This only looks up that
   * target, so it is a fast check whether it is still in range and attackable.
   */
  bool IsInRange (const proto::TargetId& id, const HexCoord& centre,
                  HexCoord::IntT l1range, Faction faction,
                  bool enemies, bool friendlies) const;

  /**
   * Returns the position of a target, or null if it is not a potential target
   * (e.g. an ancient building or a character inside a building).  Targets
   * that queries ignore have a position as well.
   */
  const HexCoord* FindPosition (const proto::TargetId& id) const;

  /**
   * Makes queries ignore the given target from now on (e.g. because it
   * was destroyed).  It keeps its position.
   */
  void Remove (const proto::TargetId& id);

};

/**
 * The targets returned by FindClosest.  This refers to the data of the
 * TargetFinder, and must not be used after it is destructed.
 */
class TargetFinder::Closest
{

private:

  /** The closest targets (and maybe the excluded one).  */
  std::vector<Span> spans;

  /**
   * Number of the targets before the excluded one if that is in spans,
   * and count otherwise.
   */
  size_t excludedRank = 0;

  /** Number of the closest targets.  */
  size_t count = 0;

  /**
   * Returns the number of targets in spans with a key up to the given one.
   */
  size_t CountUpTo (uint64_t key) const;

  Closest (const std::vector<Span>& s, uint64_t exclude);

  friend class TargetFinder;

public:

  Closest () = default;

  bool
  empty () const
  {
    return count == 0;
  }

  size_t
  size () const
  {
    return count;
  }

  /**
   * Returns the closest target at the given index in TargetKey order.
   */
  proto::TargetId operator[] (size_t i) const;

};

} // namespace pxd

#endif // DATABASE_TARGET_HPP
