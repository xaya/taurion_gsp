#ifndef PXD_SPAWN_HPP
#define PXD_SPAWN_HPP

#include "params.hpp"

#include "database/character.hpp"
#include "database/faction.hpp"
#include "mapdata/basemap.hpp"

#include <xayagame/random.hpp>

#include <string>

namespace pxd
{

/**
 * Spawns a new character on the map.  This takes care of initialising the
 * character accordingly (including determining the exact spawn position)
 * and updating the database as needed.
 *
 * This function returns a handle to the newly created character.
 */
CharacterTable::Handle SpawnCharacter (const std::string& owner, Faction f,
                                       CharacterTable& tbl, xaya::Random& rnd,
                                       const BaseMap& map,
                                       const Params& params);

} // namespace pxd

#endif // PXD_SPAWN_HPP
