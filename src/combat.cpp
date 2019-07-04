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

#include "combat.hpp"

#include "database/character.hpp"
#include "database/fighter.hpp"
#include "database/region.hpp"
#include "database/target.hpp"
#include "hexagonal/coord.hpp"

#include <algorithm>

namespace pxd
{

namespace
{

/**
 * Runs target selection for one fighter entity.
 */
void
SelectTarget (TargetFinder& targets, xaya::Random& rnd, Fighter f)
{
  const HexCoord pos = f.GetPosition ();

  const auto& data = f.GetCombatData ();
  HexCoord::IntT range = 0;
  for (const auto& attack : data.attacks ())
    {
      CHECK_GT (attack.range (), 0);
      range = std::max<HexCoord::IntT> (range, attack.range ());
    }
  if (range == 0)
    {
      CHECK_EQ (data.attacks_size (), 0);
      VLOG (1) << "Fighter at " << pos << " has no attacks";
      return;
    }

  HexCoord::IntT closestRange;
  std::vector<proto::TargetId> closestTargets;

  targets.ProcessL1Targets (pos, range, f.GetFaction (),
    [&] (const HexCoord& c, const proto::TargetId& id)
    {
      const HexCoord::IntT curDist = HexCoord::DistanceL1 (pos, c);
      if (closestTargets.empty () || curDist < closestRange)
        {
          closestRange = curDist;
          closestTargets = {id};
          return;
        }

      if (curDist == closestRange)
        {
          closestTargets.push_back (id);
          return;
        }

      CHECK_GT (curDist, closestRange);
    });

  VLOG (1)
      << "Found " << closestTargets.size () << " targets in closest range "
      << closestRange << " around " << pos;

  if (closestTargets.empty ())
    {
      f.ClearTarget ();
      return;
    }

  const unsigned ind = rnd.NextInt (closestTargets.size ());
  f.SetTarget (closestTargets[ind]);
}

} // anonymous namespace

void
FindCombatTargets (Database& db, xaya::Random& rnd)
{
  CharacterTable characters(db);
  FighterTable fighters(characters);
  TargetFinder targets(db);

  fighters.ProcessAll ([&] (Fighter f)
    {
      SelectTarget (targets, rnd, std::move (f));
    });
}

namespace
{

/**
 * Deals damage for one fighter with a target to the respective target.
 * Adds the target to the vector of dead fighters if it had its HP reduced
 * to zero and is now dead.
 */
void
DealDamage (FighterTable& fighters, DamageLists& dl, xaya::Random& rnd,
            Fighter f, std::vector<proto::TargetId>& dead)
{
  const auto& target = f.GetTarget ();
  Fighter tf = fighters.GetForTarget (target);
  const auto dist = HexCoord::DistanceL1 (f.GetPosition (), tf.GetPosition ());
  const auto& cd = f.GetCombatData ();

  unsigned dmg = 0;
  for (const auto& attack : cd.attacks ())
    {
      if (dist > static_cast<int> (attack.range ()))
        continue;

      CHECK_LE (attack.min_damage (), attack.max_damage ());
      const auto n = attack.max_damage () - attack.min_damage () + 1;
      dmg += attack.min_damage () + rnd.NextInt (n);
    }

  if (dmg == 0)
    {
      VLOG (1) << "No damage done to target:\n" << target.DebugString ();
      return;
    }
  VLOG (1)
      << "Dealing " << dmg << " damage to target:\n" << target.DebugString ();

  const auto attackerId = f.GetId ();
  if (attackerId.type () == proto::TargetId::TYPE_CHARACTER
        && target.type () == proto::TargetId::TYPE_CHARACTER)
    dl.AddEntry (target.id (), attackerId.id ());

  auto& hp = tf.MutableHP ();

  const unsigned shieldDmg = std::min (dmg, hp.shield ());
  hp.set_shield (hp.shield () - shieldDmg);
  dmg -= shieldDmg;

  const unsigned armourDmg = std::min (dmg, hp.armour ());
  hp.set_armour (hp.armour () - armourDmg);
  dmg -= armourDmg;

  VLOG (1) << "Total damage done: " << (shieldDmg + armourDmg);
  VLOG (1) << "Remaining total HP: " << (hp.armour () + hp.shield ());
  if (shieldDmg + armourDmg > 0 && hp.armour () + hp.shield () == 0)
    {
      /* Regenerated partial HP are ignored (i.e. you die even with 999/1000
         partial HP).  Just make sure that the partial HP are not full yet
         due to some bug.  */
      CHECK_LT (hp.shield_mhp (), 1000);
      dead.push_back (target);
    }
}

} // anonymous namespace

std::vector<proto::TargetId>
DealCombatDamage (Database& db, DamageLists& dl, xaya::Random& rnd)
{
  CharacterTable characters(db);
  FighterTable fighters(characters);

  std::vector<proto::TargetId> dead;
  fighters.ProcessWithTarget ([&] (Fighter f)
    {
      DealDamage (fighters, dl, rnd, std::move (f), dead);
    });

  return dead;
}

void
ProcessKills (Database& db, DamageLists& dl,
              const std::vector<proto::TargetId>& dead,
              const BaseMap& map)
{
  CharacterTable characters(db);
  RegionsTable regions(db);

  for (const auto& id : dead)
    switch (id.type ())
      {
      case proto::TargetId::TYPE_CHARACTER:
        {
          auto c = characters.GetById (id.id ());

          /* If the character was prospecting some region, cancel that
             operation and mark the region as not being prospected.  */
          if (c->GetProto ().has_prospection ())
            {
              const auto& pos = c->GetPosition ();
              const auto regionId = map.Regions ().GetRegionId (pos);

              LOG (INFO)
                  << "Killed character " << id.id ()
                  << " was prospecting region " << regionId
                  << ", cancelling";

              auto r = regions.GetById (regionId);
              CHECK_EQ (r->GetProto ().prospecting_character (), id.id ());
              r->MutableProto ().clear_prospecting_character ();
            }

          c.reset ();
          dl.RemoveCharacter (id.id ());
          characters.DeleteById (id.id ());
          break;
        }

      default:
        LOG (FATAL)
            << "Invalid target type killed: " << static_cast<int> (id.type ());
      }
}

namespace
{

/**
 * Applies HP regeneration (if any) to a given fighter.
 */
void
RegenerateFighterHP (Fighter f)
{
  const auto& cd = f.GetCombatData ();
  const auto& hp = f.GetHP ();

  /* Make sure to return early if there is no regeneration at all.  This
     ensures that we are not doing unnecessary database updates triggered
     through MutableHP().  */
  if (cd.shield_regeneration_mhp () == 0
        || hp.shield () == cd.max_hp ().shield ())
    return;

  unsigned shield = hp.shield ();
  unsigned mhp = hp.shield_mhp ();

  mhp += cd.shield_regeneration_mhp ();
  shield += mhp / 1000;
  mhp %= 1000;

  if (shield >= cd.max_hp ().shield ())
    {
      shield = cd.max_hp ().shield ();
      mhp = 0;
    }

  auto& mutableHP = f.MutableHP ();
  mutableHP.set_shield (shield);
  mutableHP.set_shield_mhp (mhp);
}

} // anonymous namespace

void
RegenerateHP (Database& db)
{
  CharacterTable characters(db);
  FighterTable fighters(characters);

  fighters.ProcessAll ([] (Fighter f)
    {
      RegenerateFighterHP (std::move (f));
    });
}

void
AllHpUpdates (Database& db, FameUpdater& fame, xaya::Random& rnd,
              const BaseMap& map)
{
  const auto dead = DealCombatDamage (db, fame.GetDamageLists (), rnd);
  for (const auto& id : dead)
    fame.UpdateForKill (id);
  ProcessKills (db, fame.GetDamageLists (), dead, map);
  RegenerateHP (db);
}

} // namespace pxd
