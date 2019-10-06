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

#ifndef PXD_PROSPECTING_HPP
#define PXD_PROSPECTING_HPP

#include "params.hpp"

#include "database/character.hpp"
#include "database/database.hpp"
#include "database/region.hpp"
#include "mapdata/basemap.hpp"

#include <xayautil/random.hpp>

namespace pxd
{

/**
 * Fills in the initial data for the prizes table from the prizes defined
 * in the game parameters.
 */
void InitialisePrizes (Database& db, const Params& params);

/**
 * Checks if the given region can be prospected by the given character
 * at the moment.
 */
bool CanProspectRegion (const Character& c, const Region& r);

/**
 * Finishes a done prospecting operation by the given character.  If the
 * competition is still active (not yet past the end time), then also
 * prizes can be won.
 */
void FinishProspecting (Character& c, Database& db, RegionsTable& regions,
                        xaya::Random& rnd,
                        unsigned blockHeight, int64_t timestamp,
                        const Params& params, const BaseMap& map);

} // namespace pxd

#endif // PXD_PROSPECTING_HPP
