/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#include "amount.hpp"
#include "dynobstacles.hpp"
#include "params.hpp"

#include "database/account.hpp"
#include "database/character.hpp"
#include "database/database.hpp"
#include "database/inventory.hpp"
#include "database/region.hpp"
#include "mapdata/basemap.hpp"

#include <xayautil/random.hpp>

#include <json/json.h>

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
 * Base class for MoveProcessor (handling confirmed moves) and PendingProcessor
 * (for processing pending moves).  It holds some common stuff for both
 * as well as some basic logic for processing some moves.
 */
class BaseMoveProcessor
{

protected:

  /** Parameters for the current situation.  */
  const Params& params;

  /** BaseMap instance that can be used.  */
  const BaseMap& map;

  /**
   * Block height for which the moves are parsed.  If this is used for pending
   * moves, then it is the assumed next block height (i.e. current plus one).
   */
  const unsigned height;

  /**
   * The Database handle we use for making any changes (and looking up the
   * current state while validating moves).
   */
  Database& db;

  /** Access handle for the accounts database table.  */
  AccountsTable accounts;

  /** Access handle for the characters table in the DB.  */
  CharacterTable characters;

  /** Access handle for ground loot.  */
  GroundLootTable groundLoot;

  /** Access to the regions table.  */
  RegionsTable regions;

  explicit BaseMoveProcessor (Database& d, const Params& p, const BaseMap& m,
                              const unsigned h)
    : params(p), map(m), height(h), db(d),
      accounts(db), characters(db), groundLoot(db), regions(db)
  {}

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
   * Parses and verifies a potential update to the character waypoints
   * in the update JSON.  Returns true if a valid waypoint update was found,
   * in which case wp will be set accordingly.
   */
  static bool ParseCharacterWaypoints (const Character& c,
                                       const Json::Value& upd,
                                       std::vector<HexCoord>& wp);

  /**
   * Parses and verifies a potential prospecting command.  Returns true if the
   * character will start prospecting (and sets the prospected region ID) and
   * false otherwise.
   */
  bool ParseCharacterProspecting (const Character& c, const Json::Value& upd,
                                  Database::IdT& regionId);

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
   * This function is called when TryCharacterCreation found a creation that
   * is valid and should be performed.
   */
  virtual void
  PerformCharacterCreation (const std::string& name, Faction f)
  {}

  /**
   * This function is called when TryCharacterUpdates found an update that
   * should actually be performed.
   */
  virtual void
  PerformCharacterUpdate (Character& c, const Json::Value& upd)
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

  /** Dynamic obstacle layer, used for spawning characters.  */
  DynObstacles& dyn;

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
   * Processes a command to start prospecting at the character's current
   * location on the map.
   */
  void MaybeStartProspecting (Character& c, const Json::Value& upd);

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

protected:

  void PerformCharacterCreation (const std::string& name, Faction f) override;
  void PerformCharacterUpdate (Character& c, const Json::Value& mv) override;

public:

  explicit MoveProcessor (Database& d, DynObstacles& dyo, xaya::Random& r,
                          const Params& p, const BaseMap& m, const unsigned h)
    : BaseMoveProcessor(d, p, m, h),
      dyn(dyo), rnd(r)
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
