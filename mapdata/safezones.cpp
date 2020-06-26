/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "safezones.hpp"

#include "hexagonal/ring.hpp"

namespace pxd
{

SafeZones::SafeZones (const RoConfig& cfg)
  : data(new uint8_t[ARRAY_SIZE])
{
  uint8_t* mutData = const_cast<uint8_t*> (data);
  std::fill (mutData, mutData + ARRAY_SIZE, 0);

  for (const auto& sz : cfg->safe_zones ())
    {
      const HexCoord centre(sz.centre ().x (), sz.centre ().y ());

      Entry e;
      if (sz.has_faction ())
        {
          const auto f = FactionFromString (sz.faction ());
          switch (f)
            {
            case Faction::RED:
            case Faction::GREEN:
            case Faction::BLUE:
              e = static_cast<Entry> (f);
              break;
            default:
              LOG (FATAL)
                  << "Invalid faction defined for starter zone: "
                  << sz.faction ();
              break;
            }
        }
      else
        e = Entry::NEUTRAL;

      const uint8_t val = static_cast<uint8_t> (e);
      CHECK_LE (val, 0x0F);

      for (unsigned r = 0; r <= sz.radius (); ++r)
        for (const auto& c : L1Ring (centre, r))
          {
            const auto old = GetEntry (c);
            CHECK (old == Entry::NONE)
                << "Overlapping safe zones at " << c
                << ", previous value " << static_cast<int> (old);

            size_t ind;
            unsigned shift;
            GetPosition (c, ind, shift);

            mutData[ind] |= (val << shift);
          }
    }
}

SafeZones::~SafeZones ()
{
  delete[] data;
}

} // namespace pwd
