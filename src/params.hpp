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

#ifndef PXD_PARAMS_HPP
#define PXD_PARAMS_HPP

#include "hexagonal/coord.hpp"
#include "database/amount.hpp"
#include "database/faction.hpp"
#include "database/inventory.hpp"

#include <xayagame/gamelogic.hpp>

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
   * Returns the ordered list of available prizes for prospecting in the
   * demo competition.
   */
  const std::vector<PrizeData>& ProspectingPrizes () const;

  /**
   * Returns true if prizes should be won with lower probability.
   */
  bool IsLowPrizeZone (const HexCoord& pos) const;

  /**
   * Returns the chance for reverse-engineering success (as N in 1/N) based
   * on the already existing number of blueprints.
   */
  unsigned RevEngSuccessChance (unsigned existingBp) const;

  /**
   * Returns the spawn centre and radius for the given faction.
   */
  HexCoord SpawnArea (Faction f, HexCoord::IntT& radius) const;

  /**
   * Returns true if god-mode commands are allowed (on regtest).
   */
  bool GodModeEnabled () const;

};

} // namespace pxd

#endif // PXD_PARAMS_HPP
