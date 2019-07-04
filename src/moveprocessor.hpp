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

#include "database/character.hpp"
#include "database/database.hpp"
#include "database/region.hpp"
#include "mapdata/basemap.hpp"

#include <xayautil/random.hpp>

#include <json/json.h>

namespace pxd
{

/**
 * Class that handles processing of all moves made in a block.
 */
class MoveProcessor
{

private:

  /** Basemap instance that can be used.  */
  const BaseMap& map;

  /** Parameters for the current situation.  */
  const Params& params;

  /**
   * The Database handle we use for making any changes (and looking up the
   * current state while validating moves).
   */
  Database& db;

  /** Dynamic obstacle layer, used for spawning characters.  */
  DynObstacles& dyn;

  /** Handle for random numbers.  */
  xaya::Random& rnd;

  /** Access handle for the characters table in the DB.  */
  CharacterTable characters;

  /** Access to the regions table.  */
  RegionsTable regions;

  /**
   * Processes the move corresponding to one transaction.
   */
  void ProcessOne (const Json::Value& moveObj);

  /**
   * Processes commands to create new characters in the given move.
   */
  void HandleCharacterCreation (const std::string& name, const Json::Value& mv,
                                Amount paidToDev);

  /**
   * Processes commands to make changes to existing characters.
   */
  void HandleCharacterUpdate (const std::string& name, const Json::Value& mv);

  /**
   * Processes one admin command.
   */
  void ProcessOneAdmin (const Json::Value& cmd);

  /**
   * Handles a god-mode admin command, if any.  These are used only for
   * integration testing, so that this will only be done on regtest.
   */
  void HandleGodMode (const Json::Value& cmd);

public:

  explicit MoveProcessor (Database& d, DynObstacles& dyo, xaya::Random& r,
                          const Params& p, const BaseMap& m)
    : map(m), params(p),
      db(d), dyn(dyo), rnd(r),
      characters(db), regions(db)
  {}

  MoveProcessor () = delete;
  MoveProcessor (const MoveProcessor&) = delete;
  void operator= (const MoveProcessor&) = delete;

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
