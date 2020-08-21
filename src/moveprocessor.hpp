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

#ifndef PXD_MOVEPROCESSOR_HPP
#define PXD_MOVEPROCESSOR_HPP

#include "context.hpp"
#include "dynobstacles.hpp"
#include "services.hpp"

#include "database/account.hpp"
#include "database/amount.hpp"
#include "database/building.hpp"
#include "database/character.hpp"
#include "database/database.hpp"
#include "database/inventory.hpp"
#include "database/itemcounts.hpp"
#include "database/moneysupply.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"
#include "mapdata/basemap.hpp"
#include "proto/building.pb.h"
#include "proto/roconfig.hpp"

#include <xayautil/random.hpp>

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace pxd
{

/**
 * The maximum valid chosen speed value for a move.  This is consensus
 * relevant, as it determines what moves will be considered valid.  The value
 * is small enough to avoid overflowing the uint32 proto field, but it is also
 * large enough to not be a restriction in practice (1k tiles per block).
 */
static constexpr unsigned MAX_CHOSEN_SPEED = 1'000'000;

/**
 * The maximum amount of vCHI in a move.  This is consensus relevant.
 * The value here is actually the total cap on vCHI (although that's not
 * relevant in this context).
 */
static constexpr Amount MAX_COIN_AMOUNT = 100'000'000'000;

/** Amounts of fungible items.  */
using FungibleAmountMap = std::map<std::string, Quantity>;

/**
 * Raw data for a coin transfer and burn command.
 */
struct CoinTransferBurn
{

  /** Amount of coins transferred to each account.  */
  std::map<std::string, Amount> transfers;

  /** Amount of coins burnt.  */
  Amount burnt = 0;

  CoinTransferBurn () = default;
  CoinTransferBurn (CoinTransferBurn&&) = default;
  CoinTransferBurn (const CoinTransferBurn&) = default;

  void operator= (const CoinTransferBurn&) = delete;

};

/**
 * Base class for MoveProcessor (handling confirmed moves) and PendingProcessor
 * (for processing pending moves).  It holds some common stuff for both
 * as well as some basic logic for processing some moves.
 */
class BaseMoveProcessor
{

protected:

  /** Processing context data.  */
  const Context& ctx;

  /**
   * The Database handle we use for making any changes (and looking up the
   * current state while validating moves).
   */
  Database& db;

  /**
   * Dynamic obstacle layer, used for spawning characters and placing buildings.
   * This is costly to create, and thus we use a passed-in reference instead
   * of having our own instance.  (Also for processing blocks, it needs to
   * stick around in the modified form for post-processing like movement.)
   */
  DynObstacles& dyn;

  /** Access handle for the accounts database table.  */
  AccountsTable accounts;

  /** Access handle for the buildings database table.  */
  BuildingsTable buildings;

  /** Access handle for the characters table in the DB.  */
  CharacterTable characters;

  /** Access handle for ground loot.  */
  GroundLootTable groundLoot;

  /** Access handle for building inventories.  */
  BuildingInventoriesTable buildingInv;

  /** Item counts table.  */
  ItemCounts itemCounts;

  /** MoneySupply database table.  */
  MoneySupply moneySupply;

  /** Ongoing operations table.  */
  OngoingsTable ongoings;

  /** Access to the regions table.  */
  RegionsTable regions;

  explicit BaseMoveProcessor (Database& d, DynObstacles& o, const Context& c);

  /**
   * Parses some basic stuff from a move JSON object.  This extracts the
   * actual move JSON value, the name and the dev payment.  The function
   * returns true if the extraction went well so far and the move
   * may be processed further.
   */
  bool ExtractMoveBasics (const Json::Value& moveObj,
                          std::string& name, Json::Value& mv,
                          Amount& paidToDev) const;

  /**
   * Parses and validates a move to transfer and burn coins (vCHI).  Returns
   * true if at least one part of the transfer/burn was parsed successfully
   * and needs to be executed.
   */
  bool ParseCoinTransferBurn (const Account& a, const Json::Value& moveObj,
                              CoinTransferBurn& op);

  /**
   * Parses and verifies a potential update to the character waypoints
   * in the update JSON.  Returns true if a valid waypoint update was found,
   * in which case wp will be set accordingly.
   */
  static bool ParseCharacterWaypoints (const Character& c,
                                       const Json::Value& upd,
                                       std::vector<HexCoord>& wp);

  /**
   * Parses and verifies a potential update to the character's
   * "enter building" value.  On success, buildingId will be set
   * to the ID of the building to enter, or EMPTY_ID if the move
   * is to cancel any current enter command.
   */
  bool ParseEnterBuilding (const Character& c, const Json::Value& upd,
                           Database::IdT& buildingId);

  /**
   * Parses and verifies a potential update to exit the current building.
   */
  static bool ParseExitBuilding (const Character& c, const Json::Value& upd);

  /**
   * Parses and validates the content of a drop or pick-up character command.
   * Returns the fungible items and their quantities to drop or pick up.
   */
  FungibleAmountMap ParseDropPickupFungible (const Json::Value& cmd) const;

  /**
   * Parses and verifies a potential prospecting command.  Returns true if the
   * character will start prospecting (and sets the prospected region ID) and
   * false otherwise.
   */
  bool ParseCharacterProspecting (const Character& c, const Json::Value& upd,
                                  Database::IdT& regionId);

  /**
   * Parses and verifies a potential command to start mining.  Returns true
   * if the character will start mining, i.e. all looks valid.
   */
  bool ParseCharacterMining (const Character& c, const Json::Value& upd,
                             Database::IdT& regionId);

  /**
   * Parses and verifies a potential "change vehicle" command.
   */
  bool ParseChangeVehicle (const Character& c, const Json::Value& upd,
                           std::string& vehicle);

  /**
   * Parses and verifies a potential command to set fitments on a character's
   * current vehicle.
   */
  bool ParseSetFitments (const Character& c, const Json::Value& upd,
                         std::vector<std::string>& fitments);

  /**
   * Parses and verifies a potential command to place a building foundation
   * at the character's position.
   */
  bool ParseFoundBuilding (const Character& c, const Json::Value& upd,
                           std::string& type,
                           proto::ShapeTransformation& trafo);

  /**
   * Parses and verifies a potential character creation as part of the
   * given move.  For all valid creations, PerformCharacterCreation
   * is called with the relevant data.
   */
  void TryCharacterCreation (const std::string& name, const Json::Value& mv,
                             Amount paidToDev);

  /**
   * Parses and verifies potential character updates as part of the
   * given move.  This extracts all commands to update characters
   * and verifies if they are in general valid (e.g. the character exists
   * and is owned by the user sending the move).  For all which are valid,
   * PerformCharacterUpdate will be called.
   */
  void TryCharacterUpdates (const std::string& name, const Json::Value& mv);

  /**
   * Parses and verifies potential building updates as part of a move
   * by the building's owner.  This does some general verification and
   * parsing, and calls through to PerformBuildingUpdates with the
   * data per building that is valid.
   */
  void TryBuildingUpdates (const std::string& name, const Json::Value& mv);

  /**
   * Parses and handles a potential character update that triggers
   * mobile refining.
   */
  void TryMobileRefining (Character& c, const Json::Value& upd);

  /**
   * Parses and handles a potential move with requested service operations.
   * Each valid operation will be passed to PerformServiceOperation for
   * either execution or recording in the pending state.
   */
  void TryServiceOperations (const std::string& name, const Json::Value& mv);

  /**
   * This function is called when TryCharacterCreation found a creation that
   * is valid and should be performed.
   */
  virtual void
  PerformCharacterCreation (Account& acc, Faction f)
  {}

  /**
   * This function is called when TryCharacterUpdates found an update that
   * should actually be performed.
   */
  virtual void
  PerformCharacterUpdate (Character& c, const Json::Value& upd)
  {}

  /**
   * This function is called when TryBuildingUpdates found a valid update
   * that should be performed.
   */
  virtual void
  PerformBuildingUpdate (Building& b, const Json::Value& upd)
  {}

  /**
   * This function is called when TryServiceOperations found a valid
   * service operation.
   */
  virtual void
  PerformServiceOperation (ServiceOperation& op)
  {}

public:

  virtual ~BaseMoveProcessor () = default;

  BaseMoveProcessor () = delete;
  BaseMoveProcessor (const BaseMoveProcessor&) = delete;
  void operator= (const BaseMoveProcessor&) = delete;

};

/**
 * Class that handles processing of all moves made in a block.
 */
class MoveProcessor : public BaseMoveProcessor
{

private:

  /** Handle for random numbers.  */
  xaya::Random& rnd;

  /**
   * Processes the move corresponding to one transaction.
   */
  void ProcessOne (const Json::Value& moveObj);

  /**
   * Processes one admin command.
   */
  void ProcessOneAdmin (const Json::Value& cmd);

  /**
   * Handles a god-mode admin command, if any.  These are used only for
   * integration testing, so that this will only be done on regtest.
   */
  void HandleGodMode (const Json::Value& cmd);

  /**
   * Transfers the given character if the update JSON contains a request
   * to do so.
   */
  void MaybeTransferCharacter (Character& c, const Json::Value& upd);

  /**
   * Sets the character's waypoints if a valid command for starting a move
   * is there.
   */
  static void MaybeSetCharacterWaypoints (Character& c, const Json::Value& upd);

  /**
   * Processes a command to set (or clear) a character's "enter building".
   */
  void MaybeEnterBuilding (Character& c, const Json::Value& upd);

  /**
   * Processes a command to exit a building the character is in.
   */
  void MaybeExitBuilding (Character& c, const Json::Value& upd);

  /**
   * Processes a command to start prospecting at the character's current
   * location on the map.
   */
  void MaybeStartProspecting (Character& c, const Json::Value& upd);

  /**
   * Processes a command to start mining at the current location.
   */
  void MaybeStartMining (Character& c, const Json::Value& upd);

  /**
   * Processes a command to change character vehicle.
   */
  void MaybeChangeVehicle (Character& c, const Json::Value& upd);

  /**
   * Processes a command to set fitments.
   */
  void MaybeSetFitments (Character& c, const Json::Value& upd);

  /**
   * Processes a command to build a foundation.
   */
  void MaybeFoundBuilding (Character& c, const Json::Value& upd);

  /**
   * Processes a command to drop loot from the character's inventory
   * onto the ground.
   */
  void MaybeDropLoot (Character& c, const Json::Value& cmd);

  /**
   * Processes a command to pick up loot from the ground.
   */
  void MaybePickupLoot (Character& c, const Json::Value& cmd);

  /**
   * Tries to handle an account initialisation (choosing faction) from
   * the given move.
   */
  void MaybeInitAccount (const std::string& name, const Json::Value& init);

  /**
   * Tries to handle a move that updates an account.
   */
  void TryAccountUpdate (const std::string& name, const Json::Value& upd);

  /**
   * Tries to handle a coin (vCHI) transfer / burn operation.
   */
  void TryCoinOperation (const std::string& name, const Json::Value& mv);

protected:

  void PerformCharacterCreation (Account& acc, Faction f) override;
  void PerformCharacterUpdate (Character& c, const Json::Value& mv) override;
  void PerformBuildingUpdate (Building& b, const Json::Value& mv) override;
  void PerformServiceOperation (ServiceOperation& op) override;

public:

  explicit MoveProcessor (Database& d, DynObstacles& dyo, xaya::Random& r,
                          const Context& c)
    : BaseMoveProcessor(d, dyo, c), rnd(r)
  {}

  /**
   * Processes all moves from the given JSON array.
   */
  void ProcessAll (const Json::Value& moveArray);

  /**
   * Processes all admin commands sent in a block.
   */
  void ProcessAdmin (const Json::Value& arr);

};

} // namespace pxd

#endif // PXD_MOVEPROCESSOR_HPP
