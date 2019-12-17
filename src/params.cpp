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

Amount
Params::CharacterCost () const
{
  return 50 * COIN;
}

unsigned
Params::CharacterLimit () const
{
  return 20;
}

HexCoord::IntT
Params::MaximumWaypointL1Distance () const
{
  return 100;
}

unsigned
Params::BlockedStepRetries () const
{
  return 10;
}

unsigned
Params::DamageListBlocks () const
{
  return 100;
}

unsigned
Params::ProspectingBlocks () const
{
  return 10;
}

unsigned
Params::ProspectionExpiryBlocks () const
{
  switch (chain)
    {
    case xaya::Chain::MAIN:
    case xaya::Chain::TEST:
      return 5'000;
    case xaya::Chain::REGTEST:
      return 100;
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
    {"1%% shares in a soccer club", 10, 10559},
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

bool
Params::IsBankingArea (const HexCoord& pos) const
{
  static const HexCoord centres[] =
    {
      HexCoord (-125, 810),
      HexCoord (-1'301, 902),
      HexCoord (-637, -291),
    };
  static constexpr HexCoord::IntT maxDist = 50;

  for (const auto& c : centres)
    if (HexCoord::DistanceL1 (pos, c) <= maxDist)
      return true;

  return false;
}

const Params::BankingSetData&
Params::BankingSet () const
{
  static const BankingSetData data =
    {
      {"raw a", 20},
      {"raw b", 20},
      {"raw c", 20},
      {"raw d", 20},
      {"raw e", 20},
      {"raw f", 20},
      {"raw g", 20},
      {"raw h", 20},
      {"raw i", 20},
    };

  return data;
}

HexCoord
Params::SpawnArea (const Faction f, HexCoord::IntT& radius) const
{
  radius = 50;

  switch (f)
    {
    case Faction::RED:
      return HexCoord (1993, -2636);

    case Faction::GREEN:
      return HexCoord (-3430, 1793);

    case Faction::BLUE:
      return HexCoord (571, 2609);

    default:
      LOG (FATAL) << "Invalid faction: " << static_cast<int> (f);
    }
}

void
Params::InitCharacterStats (const Faction f,
                            proto::RegenData& regen, proto::Character& pb) const
{
  auto* miningRate = pb.mutable_mining ()->mutable_rate ();
  miningRate->set_min (0);
  miningRate->set_max (5);

  auto* cd = pb.mutable_combat_data ();
  proto::Attack* attack;

  auto* maxHP = regen.mutable_max_hp ();
  maxHP->set_shield (30);

  switch (f)
    {
    case Faction::RED:
      pb.set_speed (2'100);
      pb.set_cargo_space (20);

      maxHP->set_armour (75);
      regen.set_shield_regeneration_mhp (500);

      attack = cd->add_attacks ();
      attack->set_range (7);
      attack->set_area (true);
      attack->set_min_damage (1);
      attack->set_max_damage (20);
      break;

    case Faction::BLUE:
      pb.set_speed (2'200);
      pb.set_cargo_space (20);

      maxHP->set_armour (50);
      regen.set_shield_regeneration_mhp (1'000);

      attack = cd->add_attacks ();
      attack->set_range (10);
      attack->set_min_damage (1);
      attack->set_max_damage (12);
      break;

    case Faction::GREEN:
      pb.set_speed (2'000);
      pb.set_cargo_space (30);

      maxHP->set_armour (150);
      regen.set_shield_regeneration_mhp (200);

      attack = cd->add_attacks ();
      attack->set_range (10);
      attack->set_min_damage (1);
      attack->set_max_damage (20);
      break;

    default:
      LOG (FATAL) << "Invalid faction: " << static_cast<int> (f);
    }
}

bool
Params::GodModeEnabled () const
{
  return chain == xaya::Chain::REGTEST;
}

} // namespace pxd
