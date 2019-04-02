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
  return 5 * COIN;
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
Params::ProspectingBlocks () const
{
  return 10;
}

HexCoord
Params::SpawnArea (const Faction f, HexCoord::IntT& radius) const
{
  radius = 50;

  switch (f)
    {
    case Faction::RED:
      return HexCoord (-1100, 1042);

    case Faction::GREEN:
      return HexCoord (-1042, 1265);

    case Faction::BLUE:
      return HexCoord (-1377, 1263);

    default:
      LOG (FATAL) << "Invalid faction: " << static_cast<int> (f);
    }
}

void
Params::InitCharacterStats (proto::Character& pb) const
{
  pb.set_speed (750);

  auto* cd = pb.mutable_combat_data ();
  auto* attack = cd->add_attacks ();
  attack->set_range (10);
  attack->set_max_damage (1);
  attack = cd->add_attacks ();
  attack->set_range (1);
  attack->set_max_damage (5);

  auto* maxHP = cd->mutable_max_hp ();
  maxHP->set_armour (100);
  maxHP->set_shield (30);
  cd->set_shield_regeneration_mhp (500);
}

bool
Params::GodModeEnabled () const
{
  return chain == xaya::Chain::REGTEST;
}

} // namespace pxd
