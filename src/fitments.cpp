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

#include "proto/roitems.hpp"

namespace pxd
{

namespace
{

/**
 * Initialises the character stats from the base values with the
 * given vehicle.
 */
void
InitCharacterStats (Character& c, const proto::ItemData::VehicleData& data)
{
  auto& pb = c.MutableProto ();
  pb.set_cargo_space (data.cargo_space ());
  pb.set_speed (data.speed ());
  *pb.mutable_combat_data () = data.combat_data ();
  c.MutableRegenData () = data.regen_data ();
  *pb.mutable_mining ()->mutable_rate () = data.mining_rate ();
}

/**
 * Applies all fitments from the character proto onto the base stats
 * in there already.
 */
void
ApplyFitments (Character& c)
{
  /* Boosts from stat modifiers are not compounding.  Thus we total up
     each modifier first and only apply them at the end.  */
  StatModifier cargo, speed;
  StatModifier maxArmour, maxShield;
  StatModifier shieldRegen;
  StatModifier range, damage;

  auto& pb = c.MutableProto ();
  for (const auto& f : pb.fitments ())
    {
      const auto fItemData = RoItemData (f);
      CHECK (fItemData.has_fitment ())
          << "Non-fitment type " << f << " on character " << c.GetId ();
      const auto& fitment = fItemData.fitment ();

      if (fitment.has_attack ())
        *pb.mutable_combat_data ()->add_attacks () = fitment.attack ();

      cargo += fitment.cargo_space ();
      speed += fitment.speed ();
      maxArmour += fitment.max_armour ();
      maxShield += fitment.max_shield ();
      shieldRegen += fitment.shield_regen ();
      range += fitment.range ();
      damage += fitment.damage ();
    }

  pb.set_cargo_space (cargo (pb.cargo_space ()));
  pb.set_speed (speed (pb.speed ()));

  auto& regen = c.MutableRegenData ();
  regen.mutable_max_hp ()->set_armour (maxArmour (regen.max_hp ().armour ()));
  regen.mutable_max_hp ()->set_shield (maxShield (regen.max_hp ().shield ()));
  regen.set_shield_regeneration_mhp (
      shieldRegen (regen.shield_regeneration_mhp ()));

  for (auto& a : *pb.mutable_combat_data ()->mutable_attacks ())
    {
      a.set_range (range (a.range ()));
      a.set_min_damage (damage (a.min_damage ()));
      a.set_max_damage (damage (a.max_damage ()));
    }
}

} // anonymous namespace

void
DeriveCharacterStats (Character& c)
{
  const auto& vehicleItemData = RoItemData (c.GetProto ().vehicle ());
  CHECK (vehicleItemData.has_vehicle ())
      << "Character " << c.GetId ()
      << " is in non-vehicle: " << c.GetProto ().vehicle ();

  InitCharacterStats (c, vehicleItemData.vehicle ());
  ApplyFitments (c);

  /* Reset the current HP back to maximum, which might have changed.  This is
     fine as we only allow fitment changes for fully repaired vehicles
     anyway (as well as for spawned characters).  */
  c.MutableHP () = c.GetRegenData ().max_hp ();
}

} // namespace pxd
