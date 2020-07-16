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

#ifndef PXD_DYNOBSTACLES_HPP
#define PXD_DYNOBSTACLES_HPP

#include "context.hpp"

#include "database/building.hpp"
#include "database/database.hpp"
#include "database/faction.hpp"
#include "hexagonal/coord.hpp"
#include "mapdata/basemap.hpp"
#include "mapdata/dyntiles.hpp"
#include "proto/building.pb.h"

namespace pxd
{

/**
 * Dynamic obstacles on the map (vehicles of different factions and buildings).
 * The data is kept in memory only.  It is initialised from the database
 * in the constructor, and must be kept up-to-date (e.g. when vehicles are
 * moving around) during the lifetime of the instance.
 */
class DynObstacles
{

private:

  /** Chain to extract the roconfig building shapes.  */
  const xaya::Chain chain;

  /** Vehicles of the red faction on the map.  */
  DynTiles<bool> red;
  /** Vehicles of the green faction on the map.  */
  DynTiles<bool> green;
  /** Vehicles of the blue faction on the map.  */
  DynTiles<bool> blue;

  /** Buildings in general.  */
  DynTiles<bool> buildings;

  /**
   * Returns the obstacle map responsible for the given faction.
   */
  DynTiles<bool>& FactionVehicles (Faction f);

  const DynTiles<bool>&
  FactionVehicles (const Faction f) const
  {
    return const_cast<DynObstacles*> (this)->FactionVehicles (f);
  }

public:

  /**
   * Constructs an "empty" instance.  This is used by the non-state RPC
   * server for "findpath".
   */
  DynObstacles (xaya::Chain c);

  /**
   * Constructs an initialised instance with all vehicles and buildings
   * from the database.
   */
  DynObstacles (Database& db, const Context& c);

  DynObstacles () = delete;
  DynObstacles (const DynObstacles&) = delete;
  void operator= (const DynObstacles&) = delete;

  /**
   * Checks whether the given tile is passable to a vehicle of the given
   * faction.  This must only be called for tiles on the map.
   */
  bool IsPassable (const HexCoord& c, Faction f) const;

  /**
   * Checks whether the given tile is entirely free (which is needed to
   * place buildings).
   */
  bool IsFree (const HexCoord& c) const;

  /**
   * Adds a new vehicle with the given faction and position.
   */
  void AddVehicle (const HexCoord& c, Faction f);

  /**
   * Removes a vehicle from the given position.
   */
  void RemoveVehicle (const HexCoord& c, Faction f);

  /**
   * Adds a building from the raw data (without requiring a Building instance).
   * Also exposes the building's shape to the caller for further processing.
   * Returns false if adding failed, e.g. because the buildings overlap.
   */
  bool AddBuilding (const std::string& type,
                    const proto::ShapeTransformation& trafo,
                    const HexCoord& pos,
                    std::vector<HexCoord>& shape);

  /**
   * Adds a new building.  CHECK-fails if something goes wrong.
   */
  void AddBuilding (const Building& b);

};

} // namespace pxd

#include "dynobstacles.tpp"

#endif // PXD_DYNOBSTACLES_HPP
