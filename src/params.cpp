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
  return 1 * COIN;
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

int64_t
Params::CompetitionEndTime () const
{
  /* 2019-10-15, 14:00 UTC */
  return 1571148000;
}

namespace
{

/** Prospecting prizes for mainnet/testnet.  */
const std::vector<Params::PrizeData> PRIZES =
  {
    {"gold", 5, 150000},
    {"silver", 50, 4000},
    {"bronze", 2000, 100},

    /* The odds for the extra pizes are chosen so that we expect a 90%
       probability to find all of them with 150k trials.  This can be computed
       with the script in contrib/competition/findOdds.m.  */
    {"shr", 60, 2139},
    {"spirit clash", 730, 196},
    {"dio", 50, 2532},
    {"1up", 20, 5791},
    {"battle racers", 3, 28184},
    {"divi", 20, 5791},
    {"dft", 50, 2532},
    {"9la necklace", 30, 4033},
    {"9la miner", 10, 10559},
    {"9la yellow", 10, 10559},
    {"9la horned", 10, 10559},
    {"snails", 20, 5791},
  };

/** Prospecting prizes for regtest (easier to find / exhaust).  */
const std::vector<Params::PrizeData> PRIZES_REGTEST =
  {
    {"gold", 3, 100},
    {"silver", 1000, 10},
    {"bronze", 0, 1},
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
      return HexCoord (-321, 2424);

    default:
      LOG (FATAL) << "Invalid faction: " << static_cast<int> (f);
    }
}

void
Params::InitCharacterStats (proto::RegenData& regen, proto::Character& pb) const
{
  pb.set_speed (3000);
  pb.set_cargo_space (1000);

  auto* cd = pb.mutable_combat_data ();
  auto* attack = cd->add_attacks ();
  attack->set_range (10);
  attack->set_min_damage (1);
  attack->set_max_damage (20);
  attack = cd->add_attacks ();
  attack->set_range (1);
  attack->set_min_damage (5);
  attack->set_max_damage (30);

  auto* maxHP = regen.mutable_max_hp ();
  maxHP->set_armour (100);
  maxHP->set_shield (30);
  regen.set_shield_regeneration_mhp (500);
}

bool
Params::GodModeEnabled () const
{
  return chain == xaya::Chain::REGTEST;
}

} // namespace pxd
