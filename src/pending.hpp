/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#ifndef PXD_PENDING_HPP
#define PXD_PENDING_HPP

#include "context.hpp"
#include "dynobstacles.hpp"
#include "moveprocessor.hpp"
#include "services.hpp"
#include "trading.hpp"

#include "database/character.hpp"
#include "database/database.hpp"
#include "database/faction.hpp"
#include "hexagonal/coord.hpp"
#include "mapdata/basemap.hpp"
#include "proto/building.pb.h"

#include <xayagame/sqlitegame.hpp>

#include <json/json.h>

#include <memory>
#include <string>
#include <vector>

namespace pxd
{

class PXLogic;

/**
 * The state of pending moves for a Taurion game.  (This holds just the
 * state and manages updates as well as JSON conversion, without being
 * the PendingMoveProcessor itself.)
 */
class PendingState
{

private:

  /**
   * Pending updates to a building.
   */
  struct BuildingState
  {

    /** The new configuration that will be scheduled.  */
    proto::Building::Config newConfig;

    /**
     * Returns the JSON representation of the pending state.
     */
    Json::Value ToJson () const;

  };

  /**
   * Pending state of one character.
   */
  struct CharacterState
  {

    /**
     * Modified waypoints (if preset).  The vector may be empty, which means
     * that we are removing any movement.
     */
    std::unique_ptr<std::vector<HexCoord>> wp;

    /** Whether or not an "enter building" command is pending.  */
    bool hasEnterBuilding = false;
    /**
     * If there is an enter building command, then this is the ID of the
     * building (or EMPTY_ID) that will be set on the character.
     */
    Database::IdT enterBuilding;

    /**
     * Set to the building the character is in when it has a pending
     * move to exit.  EMPTY_ID otherwise.
     */
    Database::IdT exitBuilding = Database::EMPTY_ID;

    /** Set to true if there is a pending pickup command.  */
    bool pickup = false;

    /** Set to true if there is a pending drop command.  */
    bool drop = false;

    /**
     * The ID of the region this character is starting to prospect.  Set to
     * RegionMap::OUT_OF_MAP if no prospection is coming.
     */
    Database::IdT prospectingRegionId = RegionMap::OUT_OF_MAP;

    /**
     * The ID of the region this character will start mining in.  Set to
     * RegionMap::OUT_OF_MAP if no mining is being started.
     */
    Database::IdT miningRegionId = RegionMap::OUT_OF_MAP;

    /** A pending move to found a building, if any (otherwise JSON null).  */
    Json::Value foundBuilding;

    /** The vehicle the character is changing to (if non-empty).  */
    Json::Value changeVehicle;

    /**
     * Placed fitments on the character, if any.  This is already in JSON
     * format for simplicity, and None if there are no fitment moves.
     */
    Json::Value fitments;

    /**
     * Returns the JSON representation of the pending state.
     */
    Json::Value ToJson () const;

  };

  /**
   * Pending state of a newly created character.
   */
  struct NewCharacter
  {

    /** The character's faction.  */
    Faction faction;

    explicit NewCharacter (const Faction f)
      : faction(f)
    {}

    /**
     * Returns the JSON representation of the character creation.
     */
    Json::Value ToJson () const;

  };

  /**
   * Pending state updates associated to an account.
   */
  struct AccountState
  {

    /** The combined coin transfer / burn for this account.  */
    std::unique_ptr<CoinTransferBurn> coinOps;

    /** Requested DEX / trading opreations (already as JSON).  */
    std::vector<Json::Value> dexOps;

    /** Requested service operations (already as JSON).  */
    std::vector<Json::Value> serviceOps;

    /**
     * Returns the JSON representation of the pending state.
     */
    Json::Value ToJson () const;

  };

  /** Pending modifications to buildings.  */
  std::map<Database::IdT, BuildingState> buildings;

  /** Pending modifications to characters.  */
  std::map<Database::IdT, CharacterState> characters;

  /** Pending creations of new characters (by account name).  */
  std::map<std::string, std::vector<NewCharacter>> newCharacters;

  /** Pending updates by account name.  */
  std::map<std::string, AccountState> accounts;

  /**
   * Returns the pending building state for the given instance, creating
   * a new empty one if needed.
   */
  BuildingState& GetBuildingState (const Building& b);

  /**
   * Returns the pending character state for the given instance, creating one
   * if we do not have it yet.  The reference follows iterator-invalidation
   * rules for "characters".
   */
  CharacterState& GetCharacterState (const Character& c);

  /**
   * Returns the pending state for the given account instance, creating a new
   * (empty) one if there is not already one.
   */
  AccountState& GetAccountState (const Account& a);

public:

  PendingState () = default;

  PendingState (const PendingState&) = delete;
  void operator= (const PendingState&) = delete;

  /**
   * Clears all pending state and resets it to "empty" (corresponding to a
   * situation without any pending moves).
   */
  void Clear ();

  /**
   * Updates the state for a new building configuration being scheduled.
   */
  void AddBuildingConfig (const Building& b,
                          const proto::Building::Config& newConfig);

  /**
   * Updates the state for waypoints found for a character in a pending move.
   * If replace is true, we erase any existing waypoints in the pending state.
   * Otherwise, we add to them.
   *
   * If the character is already pending to start prospecting, then this
   * will do nothing as a prospecting character cannot move.  If the character
   * is poised to start mining, then mining will be stopped.
   */
  void AddCharacterWaypoints (const Character& ch,
                              const std::vector<HexCoord>& wp,
                              bool replace);

  /**
   * Updates the state, adding an "enter building" command.
   */
  void AddEnterBuilding (const Character& ch, Database::IdT buildingId);

  /**
   * Updates the state, turning on the "exit building" flag.
   */
  void AddExitBuilding (const Character& ch);

  /**
   * Marks the character state as having a pending drop command.
   */
  void AddCharacterDrop (const Character& ch);

  /**
   * Marks the character state as having a pending pickup command.
   */
  void AddCharacterPickup (const Character& ch);

  /**
   * Updates the state of a character to include a pending prospecting
   * for the given region.
   *
   * A character that prospects can't move, so this will unset the pending
   * waypoints for it (if any).
   */
  void AddCharacterProspecting (const Character& ch, Database::IdT regionId);

  /**
   * Updates the state of a character to start mining in a given region.
   *
   * If the character is moving or going to prospect, then the change
   * is ignored.
   */
  void AddCharacterMining (const Character& ch, Database::IdT regionId);

  /**
   * Updates the state of a character to indiciate that it will
   * found a building.
   */
  void AddFoundBuilding (const Character& ch, const std::string& type,
                         const proto::ShapeTransformation& trafo);

  /**
   * Updates the state to add a "change vehicle" move.
   */
  void AddCharacterVehicle (const Character& ch, const std::string& vehicle);

  /**
   * Updates the state to add a move that sets fitments to the
   * given list of items.
   */
  void AddCharacterFitments (const Character& ch,
                             const std::vector<std::string>& fitments);

  /**
   * Updates the state for a new pending character creation.
   */
  void AddCharacterCreation (const std::string& name, Faction f);

  /**
   * Updates the state for a new coin transfer / burn.
   */
  void AddCoinTransferBurn (const Account& a, const CoinTransferBurn& op);

  /**
   * Updates the state for a given account, adding a new service operation.
   */
  void AddServiceOperation (const ServiceOperation& op);

  /**
   * Updates the state for a given account, adding a new DEX operation.
   */
  void AddDexOperation (const DexOperation& op);

  /**
   * Returns true if the given character has pending waypoints.
   */
  bool HasPendingWaypoints (const Character& c) const;

  /**
   * Returns the JSON representation of the pending state.
   */
  Json::Value ToJson () const;

};

/**
 * BaseMoveProcessor class that updates the pending state.  This contains the
 * main logic for PendingMoves::AddPendingMove, and is also accessible from
 * the unit tests independently of SQLiteGame.
 *
 * Instances of this class are light-weight and just contain the logic.  They
 * are created on-the-fly for processing a single move.
 */
class PendingStateUpdater : public BaseMoveProcessor
{

private:

  /** The PendingState instance that is updated.  */
  PendingState& state;

protected:

  void PerformBuildingConfigUpdate (
      Building& b, const proto::Building::Config& newConfig) override;
  void PerformCharacterCreation (Account& acc, Faction f) override;
  void PerformCharacterUpdate (Character& c, const Json::Value& upd) override;
  void PerformServiceOperation (ServiceOperation& op) override;
  void PerformDexOperation (DexOperation& op) override;

public:

  explicit PendingStateUpdater (Database& d, DynObstacles& o,
                                PendingState& s, const Context& c)
    : BaseMoveProcessor(d, o, c), state(s)
  {}

  /**
   * Processes the given move.
   */
  void ProcessMove (const Json::Value& moveObj);

};

/**
 * Processor for pending moves in Taurion.  This keeps track of some information
 * that we use in the frontend, like the modified waypoints of characters
 * and creation of new characters.
 */
class PendingMoves : public xaya::SQLiteGame::PendingMoves
{

private:

  /** The current state of pending moves.  */
  PendingState state;

  /**
   * A DynObstacles instance based on the confirmed database state.
   * This is costly to create, thus we create it on-demand and keep it cached
   * for all pending moves until the next call to Clear (when the confirmed
   * state changes).
   */
  std::unique_ptr<DynObstacles> dyn;

protected:

  void Clear () override;
  void AddPendingMove (const Json::Value& mv) override;

public:

  explicit PendingMoves (PXLogic& rules);

  Json::Value ToJson () const override;

};

} // namespace pxd

#endif // PXD_PENDING_HPP
