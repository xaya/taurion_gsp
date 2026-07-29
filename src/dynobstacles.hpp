/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2026  Autonomous Worlds Ltd

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
#include "mapdata/sparsemap.hpp"
#include "proto/building.pb.h"
#include "proto/roconfig.hpp"

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

  /** Config from which the building shapes are extracted.  */
  const RoConfig cfg;

  /** Vehicles (of any faction) on the map.  */
  SparseTileMap<unsigned> vehicles;

  /** Buildings in general.  */
  DynTiles<bool> buildings;

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
   * Returns the config this instance uses.  Callers that look up building
   * data alongside it have to use the same one, so that the two cannot end
   * up on either side of an activated config change.
   */
  const RoConfig&
  Config () const
  {
    return cfg;
  }

  /**
   * Checks if the given tile is blocked by a building.
   */
  bool IsBuilding (const HexCoord& c) const;

  /**
   * Checks if the given tile has any vehicle.
   */
  bool HasVehicle (const HexCoord& c) const;

  /**
   * Checks whether the given tile is entirely free (which is needed to
   * place buildings).
   */
  bool IsFree (const HexCoord& c) const;

  /**
   * Adds a new vehicle with the given position.
   */
  void AddVehicle (const HexCoord& c);

  /**
   * Removes a vehicle from the given position.
   */
  void RemoveVehicle (const HexCoord& c);

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

  /**
   * Removes a building.
   */
  void RemoveBuilding (const Building& b);

};

} // namespace pxd

#include "dynobstacles.tpp"

#endif // PXD_DYNOBSTACLES_HPP
