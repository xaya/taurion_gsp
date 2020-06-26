/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#ifndef MAPDATA_SAFEZONES_HPP
#define MAPDATA_SAFEZONES_HPP

#include "tiledata.hpp"

#include "database/faction.hpp"
#include "hexagonal/coord.hpp"
#include "proto/roconfig.hpp"

#include <cstddef>
#include <cstdint>

namespace pxd
{

/**
 * Class that holds a pre-computed map of which tiles are safe zones or
 * starting areas to allow quick access during path finding and combat.
 */
class SafeZones
{

private:

  /**
   * The entries stored in our map for each coordinate.  This encodes
   * all data we need for safe and starter zones, and fits into 4 bits
   * so that we can store two of each into a single byte in memory.
   */
  enum class Entry : uint8_t
  {
    NONE = 0,
    RED = static_cast<unsigned> (Faction::RED),
    GREEN = static_cast<unsigned> (Faction::GREEN),
    BLUE = static_cast<unsigned> (Faction::BLUE),
    NEUTRAL = 4,
  };

  /** Size of the total data array we need in bytes.  */
  static constexpr size_t ARRAY_SIZE = (tiledata::numTiles + 1) / 2;

  /**
   * The array of entries.  Each byte here holds two entries, and in total
   * they are organised in a row-by-row fashion like DynTiles.  We allocate
   * it dynamically to avoid issues with stack overflow.
   */
  const uint8_t* const data;

  /**
   * Reads out the entry for the given coordinate.
   */
  inline Entry GetEntry (const HexCoord& c) const;

  /**
   * For a given coordinate, returns the byte-based index into the data array
   * and the bit shift to apply to get to this coordinate's entry.
   */
  static inline void GetPosition (const HexCoord& c,
                                  size_t& ind, unsigned& shift);

public:

  /**
   * Constructs an instance based on the zone data from the given RoConfig.
   * This fills in all the data caches.
   */
  explicit SafeZones (const RoConfig& cfg);

  ~SafeZones ();

  SafeZones () = delete;
  SafeZones (const SafeZones&) = delete;
  void operator= (const SafeZones&) = delete;

  /**
   * Returns true if the given coordinate is a no-combat zone.  This is the
   * case for all factions' starter zones as well as the neutral safe zones.
   */
  inline bool IsNoCombat (const HexCoord& c) const;

  /**
   * Returns the faction for which this is a starter zone, or INVALID if
   * it is no starter zone.
   */
  inline Faction StarterFor (const HexCoord& c) const;

};

} // namespace pwd

#include "safezones.tpp"

#endif // MAPDATA_SAFEZONES_HPP
