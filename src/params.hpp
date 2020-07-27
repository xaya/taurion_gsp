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
#include "mapdata/basemap.hpp"

#include <xayagame/gamelogic.hpp>

#include <string>
#include <vector>

namespace pxd
{

/**
 * Some "parameters" for the game rules.  Instances of this class
 * extend the very basic parameters in roconfig with more complex
 * things, like "parameters" that are simple functions (but still
 * just arbitrarily-chosen game configuration).
 */
class Params
{

private:

  /** The chain for which we need parameters.  */
  const xaya::Chain chain;

  /** Basemap instance (which we need to retrieve starter zones).  */
  const BaseMap& map;

public:

  /**
   * Constructs an instance based on the given situation.
   */
  explicit Params (const xaya::Chain c, const BaseMap& m)
    : chain(c), map(m)
  {}

  Params () = delete;
  Params (const Params&) = delete;
  void operator= (const Params&) = delete;

  /**
   * Returns true if prizes should be won with lower probability.
   */
  bool IsLowPrizeZone (const HexCoord& pos) const;

  /**
   * Returns the chance for reverse-engineering success (as N in 1/N) based
   * on the already existing number of blueprints.
   */
  unsigned RevEngSuccessChance (unsigned existingBp) const;

};

} // namespace pxd

#endif // PXD_PARAMS_HPP
