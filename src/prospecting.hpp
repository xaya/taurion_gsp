#ifndef PXD_PROSPECTING_HPP
#define PXD_PROSPECTING_HPP

#include "database/character.hpp"
#include "database/region.hpp"
#include "mapdata/basemap.hpp"

namespace pxd
{

/**
 * Finishes a done prospecting operation by the given character.
 */
void FinishProspecting (Character& c, RegionsTable& regions,
                        const BaseMap& map);

} // namespace pxd

#endif // PXD_PROSPECTING_HPP
