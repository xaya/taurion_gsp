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

#ifndef PXD_PENDING_HPP
#define PXD_PENDING_HPP

#include "moveprocessor.hpp"
#include "params.hpp"

#include "database/character.hpp"
#include "database/database.hpp"
#include "database/faction.hpp"
#include "hexagonal/coord.hpp"

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
   * Pending state of one character.
   */
  struct CharacterState
  {

    /**
     * Modified waypoints (if preset).  The vector may be empty, which means
     * that we are removing any movement.
     */
    std::unique_ptr<std::vector<HexCoord>> wp;

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

  /** Pending modifications to characters.  */
  std::map<Database::IdT, CharacterState> characters;

  /** Pending creations of new characters (by account name).  */
  std::map<std::string, std::vector<NewCharacter>> newCharacters;

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
   * Updates the state for waypoints found for a character in a pending move.
   */
  void AddCharacterWaypoints (const Character& ch,
                              const std::vector<HexCoord>& wp);

  /**
   * Updates the state for a new pending character creation.
   */
  void AddCharacterCreation (const std::string& name, Faction f);

  /**
   * Returns the JSON representation of the pending state.
   */
  Json::Value ToJson () const;

};

/**
 * BaseMoveProcessor class that updates the pending state.  This contains the
 * main logic for PendingMoves::AddPendingMove, and is also accessible from
 * the unit tests independently of SQLiteGame.
 */
class PendingStateUpdater : public BaseMoveProcessor
{

private:

  /** The PendingState instance that is updated.  */
  PendingState& state;

protected:

  void PerformCharacterCreation (const std::string& name, Faction f) override;
  void PerformCharacterUpdate (Character& c, const Json::Value& upd) override;

public:

  explicit PendingStateUpdater (Database& d, PendingState& s, const Params& p)
    : BaseMoveProcessor(d, p), state(s)
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

protected:

  void Clear () override;
  void AddPendingMove (const Json::Value& mv) override;

public:

  explicit PendingMoves (PXLogic& rules);

  Json::Value ToJson () const override;

};

} // namespace pxd

#endif // PXD_PENDING_HPP
