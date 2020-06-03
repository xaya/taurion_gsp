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

#include "params.hpp"

#include <glog/logging.h>

namespace pxd
{

std::string
Params::DeveloperAddress () const
{
  /* The address returned here is the premine address controlled by the Xaya
     team for the various chains.  See also Xaya Core's src/chainparams.cpp.  */

  switch (chain)
    {
    case xaya::Chain::MAIN:
      return "DHy2615XKevE23LVRVZVxGeqxadRGyiFW4";
    case xaya::Chain::TEST:
      return "dSFDAWxovUio63hgtfYd3nz3ir61sJRsXn";
    case xaya::Chain::REGTEST:
      return "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p";
    default:
      LOG (FATAL) << "Invalid chain value: " << static_cast<int> (chain);
    }
}

namespace
{

/** Prospecting prizes for mainnet/testnet.  */
const std::vector<Params::PrizeData> PRIZES =
  {
    {"gold", 3, 200'000},
    {"silver", 5, 100'000},
    {"bronze", 10, 25'000},

    /* List of prizes for the second competition, created from a CSV export
       processed with contrib/competition.2/process-prizes.py.  */
    {"Broze crates", 3, 28184},
    {"Mounted Boomas", 5, 18765},
    {"200 DIO", 50, 2532},
    {"Type 77 selective fire assault rifle", 1, 65145},
    {"Scorpion prototype mag-rail technology gun", 1, 65145},
    {"P1 Pistol, 7mm", 1, 65145},
    {"Defender shotgun", 1, 65145},
    {"Frag Grenade", 1, 65145},
    {"Army Cow", 5, 18765},
    {"Caramel Pony", 5, 18765},
    {"Gecko Multiverse", 5, 18765},
    {"Mustard Duck", 5, 18765},
    {"Shrake", 5, 18765},
    {"Snowflake", 1, 65145},
    {"Snowball", 2, 38564},
    {"Snowfall", 2, 38564},
    {"Enjin Legend: Simon", 1, 65145},
    {"Enjin Legend: Tassio", 1, 65145},
    {"Enjin Legend: Bryana", 1, 65145},
    {"Enjin Legend: Witek", 1, 65145},
    {"Enjin Legend: Maxim", 1, 65145},
    {"RC 9MM BTC skinned", 10, 10559},
    {"30,000 SHR coins", 10, 10559},
    {"1% shares in a soccer club", 10, 10559},
    {"Warmongers", 5, 18765},
    {"Limited Edition Varag Shamans", 30, 4033},
    {"Holy Knights", 30, 4033},
    {"500 Spirit Dust", 40, 3106},
    {"Light vehicles", 2000, 73},
    {"Light canons", 2000, 73},
    {"Rare Portal Key Lv2", 2, 38564},
    {"Rare Portal Key Lv3", 2, 38564},
    {"Epic Portal Key Lv2", 1, 65145},
    {"Uncommon Arena Crystals Lv1", 2, 38564},
    {"Uncommon Arena Crystals Lv2", 2, 38564},
    {"Epic Arena Crystal Lv2", 1, 65145},
    {"1000 Uptennd Tokens", 10, 10559},
    {"SUVs", 2, 38564},
    {"War Trucks", 2, 38564},
    {"APOCALYPSE Carbon M5 MAAWS", 1, 65145},
    {"Gold Miniguns", 1, 65145},
    {"LIGHTCAMO Carbon M2c Browning", 1, 65145},
    {"Platinum M5 MAAWS", 1, 65145},
    {"100 BZN (Benzene) tokens", 10, 10559},
    {"500 ZNZ tokens", 10, 10559},

  };

/** Prospecting prizes for regtest (easier to find / exhaust).  */
const std::vector<Params::PrizeData> PRIZES_REGTEST =
  {
    {"gold", 3, 100},
    {"silver", 1000, 10},
    {"bronze", 1, 1},
  };

} // anonymous namespace

const std::vector<Params::PrizeData>&
Params::ProspectingPrizes () const
{
  switch (chain)
    {
    case xaya::Chain::MAIN:
    case xaya::Chain::TEST:
      return PRIZES;
    case xaya::Chain::REGTEST:
      return PRIZES_REGTEST;
    default:
      LOG (FATAL) << "Invalid chain value: " << static_cast<int> (chain);
    }
}

bool
Params::IsLowPrizeZone (const HexCoord& pos) const
{
  const HexCoord noPrizeCentre(58, -256);
  constexpr HexCoord::IntT noPrizeRadius = 3'000;
  return HexCoord::DistanceL1 (pos, noPrizeCentre) <= noPrizeRadius;
}

unsigned
Params::RevEngSuccessChance (const unsigned existingBp) const
{
  constexpr uint64_t fpMultiple = 1'000'000;
  constexpr uint64_t minChance = 1'000'000'000;

  uint64_t base;
  switch (chain)
    {
    case xaya::Chain::MAIN:
    case xaya::Chain::TEST:
      base = 2'000;
      break;
    case xaya::Chain::REGTEST:
      base = 1;
      break;
    default:
      LOG (FATAL) << "Invalid chain value: " << static_cast<int> (chain);
    }

  /* The base chance is then discounted by a factor of 75% (i.e. the N value
     for 1/N increased accordingly) for each existing blueprint.  The minimum
     chance (preventing mostly integer overflows) is 1/1M.

     At least on regtest with a very low base chance, we have to do the
     calculation in fixed point math (not integer) in order to get
     values above 1.  */

  base *= fpMultiple;
  for (unsigned i = 0; i < existingBp; ++i)
    {
      base = (4 * base) / 3;
      if (base >= fpMultiple * minChance)
        return minChance;
    }
  base /= fpMultiple;

  return base;
}

} // namespace pxd
