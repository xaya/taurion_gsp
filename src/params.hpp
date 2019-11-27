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

#ifndef PXD_PARAMS_HPP
#define PXD_PARAMS_HPP

#include "amount.hpp"

#include "hexagonal/coord.hpp"
#include "database/faction.hpp"
#include "database/inventory.hpp"
#include "proto/character.pb.h"

#include <xayagame/gamelogic.hpp>

#include <map>
#include <string>
#include <vector>

namespace pxd
{

/**
 * The basic parameters that determine the game rules.  Once constructed, an
 * instance of this class is immutable and can be used to retrieve the
 * parameters for a particular situation (e.g. chain).
 */
class Params
{

private:

  /** The chain for which we need parameters.  */
  const xaya::Chain chain;

public:

  /**
   * Data defining one of the prospecting prize tiers.
   */
  struct PrizeData
  {

    /**
     * The name of the prize as used in the JSON game state and for keying
     * in the database.
     */
    std::string name;

    /** Number of prizes of this type available.  */
    unsigned number;

    /** Probability to win this prize as 1 / N.  */
    unsigned probability;

  };

  /**
   * Raw data representing the amounts of each resource needed to complete
   * a "banking set".
   */
  using BankingSetData = std::unordered_map<std::string, Inventory::QuantityT>;

  /**
   * Constructs an instance based on the given situation.
   */
  explicit Params (const xaya::Chain c)
    : chain(c)
  {}

  Params () = delete;
  Params (const Params&) = delete;
  void operator= (const Params&) = delete;

  /**
   * Returns the address to which CHI for the developers should be paid.
   */
  std::string DeveloperAddress () const;

  /**
   * Returns the amount of CHI to be paid for creation of a character.
   */
  Amount CharacterCost () const;

  /**
   * Returns the maximum number of characters allowed per account.
   */
  unsigned CharacterLimit () const;

  /**
   * Returns the maximum L1 distance between waypoints for movement.
   */
  HexCoord::IntT MaximumWaypointL1Distance () const;

  /**
   * Number of retries of a blocked movement step before the movement
   * is cancelled completely.  Note that this is really the numbef of *retries*,
   * meaning that movement is only cancelled after N+1 blocked turns if N is
   * the value returned from this function.
   */
  unsigned BlockedStepRetries () const;

  /**
   * Returns the number of blocks for which a character stays on a damage list.
   */
  unsigned DamageListBlocks () const;

  /**
   * Returns the duration of prospecting in blocks.
   */
  unsigned ProspectingBlocks () const;

  /**
   * Returns the number of blocks after which a region can be reprospected
   * (if there are no other factors preventing it).
   */
  unsigned ProspectionExpiryBlocks () const;

  /**
   * UNIX timestamp of the end time when prospecting prizes are given out.
   */
  int64_t CompetitionEndTime () const;

  /**
   * Returns the ordered list of available prizes for prospecting in the
   * demo competition.
   */
  const std::vector<PrizeData>& ProspectingPrizes () const;

  /**
   * Returns true if prizes can be won at the given coordinate.
   */
  bool CanWinPrizesAt (const HexCoord& pos) const;

  /**
   * Returns true if the given location is a banking area.
   */
  bool IsBankingArea (const HexCoord& pos) const;

  /**
   * Returns the data about our banking set.
   */
  const BankingSetData& BankingSet () const;

  /**
   * Returns the spawn centre and radius for the given faction.
   */
  HexCoord SpawnArea (Faction f, HexCoord::IntT& radius) const;

  /**
   * Initialises the stats for a newly created character.  This fills in the
   * combat data and other fields in the protocol buffer as needed.
   */
  void InitCharacterStats (Faction f, proto::RegenData& regen,
                           proto::Character& pb) const;

  /**
   * Returns true if god-mode commands are allowed (on regtest).
   */
  bool GodModeEnabled () const;

};

} // namespace pxd

#endif // PXD_PARAMS_HPP
