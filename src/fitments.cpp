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

#include "fitments.hpp"

#include "modifier.hpp"

#include "proto/roconfig.hpp"

#include <map>

namespace pxd
{

bool
CheckVehicleFitments (const std::string& vehicle,
                      const std::vector<std::string>& fitments,
                      const Context& ctx)
{
  /* We first go through all fitments, and sum up what slots we need and
     what complexity is required.  We also keep track of any potential
     modification to the supported complexity.  */
  StatModifier vehicleComplexity;
  unsigned complexityRequired = 0;
  std::map<std::string, unsigned> slotsRequired;
  for (const auto& f : fitments)
    {
      const auto& fitmentData = ctx.RoConfig ().Item (f);
      CHECK (fitmentData.has_fitment ())
          << "Item type " << f << " is not a fitment";

      complexityRequired += fitmentData.complexity ();
      ++slotsRequired[fitmentData.fitment ().slot ()];

      vehicleComplexity += fitmentData.fitment ().complexity ();
    }

  /* Now check up the required stats against the vehicle.  */
  const auto& vehicleData = ctx.RoConfig ().Item (vehicle);
  CHECK (vehicleData.has_vehicle ())
      << "Item type " << vehicle << " is not a vehicle";

  const unsigned complexityAvailable
      = vehicleComplexity (vehicleData.complexity ());
  if (complexityRequired > complexityAvailable)
    {
      VLOG (1)
          << "Fitments to vehicle " << vehicle
          << " would require complexity " << complexityRequired
          << ", but only " << complexityAvailable << " is available";
      return false;
    }

  const auto& slotsAvailable = vehicleData.vehicle ().equipment_slots ();
  for (const auto& entry : slotsRequired)
    {
      const auto mit = slotsAvailable.find (entry.first);
      if (mit == slotsAvailable.end ())
        {
          VLOG (1)
              << "Vehicle " << vehicle
              << " does not have any slots of type " << entry.first;
          return false;
        }
      if (entry.second > mit->second)
        {
          VLOG (1)
              << "Fitments to vehicle " << vehicle
              << " would require " << entry.second
              << " slots of type " << entry.first
              << ", but only " << mit->second << " are available";
          return false;
        }
    }

  return true;
}

namespace
{

/**
 * Initialises the character stats from the base values with the
 * given vehicle.
 */
void
InitCharacterStats (Character& c, const proto::VehicleData& data)
{
  auto& pb = c.MutableProto ();

  pb.set_cargo_space (data.cargo_space ());
  pb.set_speed (data.speed ());
  *pb.mutable_combat_data () = data.combat_data ();
  c.MutableRegenData () = data.regen_data ();

  if (data.has_mining_rate ())
    *pb.mutable_mining ()->mutable_rate () = data.mining_rate ();
  else
    pb.clear_mining ();

  if (data.has_prospecting_blocks ())
    {
      pb.set_prospecting_blocks (data.prospecting_blocks ());
      CHECK_GT (pb.prospecting_blocks (), 0);
    }
  else
    pb.clear_prospecting_blocks ();
}

/**
 * Applies all fitments from the character proto onto the base stats
 * in there already.
 */
void
ApplyFitments (Character& c, const Context& ctx)
{
  /* Boosts from stat modifiers are not compounding.  Thus we total up
     each modifier first and only apply them at the end.  */
  StatModifier cargo, speed;
  StatModifier prospecting, mining;
  StatModifier maxArmour, maxShield;
  StatModifier shieldRegen;
  StatModifier range, damage;

  auto& pb = c.MutableProto ();
  auto* cd = pb.mutable_combat_data ();
  for (const auto& f : pb.fitments ())
    {
      const auto fItemData = ctx.RoConfig ().Item (f);
      CHECK (fItemData.has_fitment ())
          << "Non-fitment type " << f << " on character " << c.GetId ();
      const auto& fitment = fItemData.fitment ();

      if (fitment.has_attack ())
        *cd->add_attacks () = fitment.attack ();
      if (fitment.has_low_hp_boost ())
        *cd->add_low_hp_boosts () = fitment.low_hp_boost ();
      if (fitment.has_self_destruct ())
        *cd->add_self_destructs () = fitment.self_destruct ();

      cargo += fitment.cargo_space ();
      speed += fitment.speed ();
      prospecting += fitment.prospecting_blocks ();
      mining += fitment.mining_rate ();
      maxArmour += fitment.max_armour ();
      maxShield += fitment.max_shield ();
      shieldRegen += fitment.shield_regen ();
      range += fitment.range ();
      damage += fitment.damage ();
    }

  pb.set_cargo_space (cargo (pb.cargo_space ()));
  pb.set_speed (speed (pb.speed ()));

  if (pb.has_prospecting_blocks ())
    {
      int blocks = pb.prospecting_blocks ();
      CHECK_GT (blocks, 0);
      blocks = prospecting (blocks);
      blocks = std::max (1, blocks);
      CHECK_GT (blocks, 0);
      pb.set_prospecting_blocks (blocks);
    }

  if (pb.has_mining ())
    {
      auto* rate = pb.mutable_mining ()->mutable_rate ();
      rate->set_min (mining (rate->min ()));
      rate->set_max (mining (rate->max ()));
    }

  auto& regen = c.MutableRegenData ();
  regen.mutable_max_hp ()->set_armour (maxArmour (regen.max_hp ().armour ()));
  regen.mutable_max_hp ()->set_shield (maxShield (regen.max_hp ().shield ()));
  regen.set_shield_regeneration_mhp (
      shieldRegen (regen.shield_regeneration_mhp ()));

  for (auto& a : *cd->mutable_attacks ())
    {
      /* Both the targeting range and size of AoE area (if applicable)
         are modified in the same way through the "range" modifier.  */
      if (a.has_range ())
        a.set_range (range (a.range ()));
      if (a.has_area ())
        a.set_area (range (a.area ()));

      if (a.has_damage ())
        {
          auto& dmg = *a.mutable_damage ();
          dmg.set_min (damage (dmg.min ()));
          dmg.set_max (damage (dmg.max ()));
        }
    }

  for (auto& sd : *cd->mutable_self_destructs ())
    {
      sd.set_area (range (sd.area ()));

      auto& dmg = *sd.mutable_damage ();
      dmg.set_min (damage (dmg.min ()));
      dmg.set_max (damage (dmg.max ()));
    }
}

} // anonymous namespace

void
DeriveCharacterStats (Character& c, const Context& ctx)
{
  const auto& vehicleItemData = ctx.RoConfig ().Item (c.GetProto ().vehicle ());
  CHECK (vehicleItemData.has_vehicle ())
      << "Character " << c.GetId ()
      << " is in non-vehicle: " << c.GetProto ().vehicle ();

  InitCharacterStats (c, vehicleItemData.vehicle ());
  ApplyFitments (c, ctx);

  /* Reset the current HP back to maximum, which might have changed.  This is
     fine as we only allow fitment changes for fully repaired vehicles
     anyway (as well as for spawned characters).  */
  c.MutableHP () = c.GetRegenData ().max_hp ();
}

} // namespace pxd
