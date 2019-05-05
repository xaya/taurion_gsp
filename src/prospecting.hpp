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
 * Finishes a done prospecting operation by the given character.
 */
void FinishProspecting (Character& c, Database& db, RegionsTable& regions,
                        xaya::Random& rnd,
                        const Params& params, const BaseMap& map);

} // namespace pxd

#endif // PXD_PROSPECTING_HPP
