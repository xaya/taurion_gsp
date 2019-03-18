#ifndef PXD_DYNOBSTACLES_HPP
#define PXD_DYNOBSTACLES_HPP

#include "database/database.hpp"
#include "database/faction.hpp"
#include "hexagonal/coord.hpp"
#include "mapdata/basemap.hpp"
#include "mapdata/dyntiles.hpp"

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

  /** Vehicles of the red faction on the map.  */
  DynTiles<bool> red;
  /** Vehicles of the green faction on the map.  */
  DynTiles<bool> green;
  /** Vehicles of the blue faction on the map.  */
  DynTiles<bool> blue;

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
   * Constructs an initialised instance with all vehicles and buildings
   * from the database.
   */
  DynObstacles (Database& db);

  DynObstacles () = delete;
  DynObstacles (const DynObstacles&) = delete;
  void operator= (const DynObstacles&) = delete;

  /**
   * Checks whether the given tile is passable to a vehicle of the given
   * faction.  This must only be called for tiles on the map.
   */
  bool IsPassable (const HexCoord& c, Faction f) const;

  /**
   * Adds a new vehicle with the given faction and position.
   */
  void AddVehicle (const HexCoord& c, Faction f);

  /**
   * Removes a vehicle from the given position.
   */
  void RemoveVehicle (const HexCoord& c, Faction f);

};

} // namespace pxd

#endif // PXD_DYNOBSTACLES_HPP
