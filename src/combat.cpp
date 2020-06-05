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

#include "combat.hpp"

#include "modifier.hpp"

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/fighter.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"
#include "database/target.hpp"
#include "hexagonal/coord.hpp"

#include <algorithm>
#include <map>
#include <vector>

namespace pxd
{

namespace
{

/**
 * Chance (in percent) that an inventory position inside a destroyed building
 * will drop on the ground instead of being destroyed.
 */
constexpr const unsigned BUILDING_INVENTORY_DROP_PERCENT = 30;

/**
 * Modifications to combat-related stats.
 */
struct CombatModifier
{

  /** Modification of combat damage.  */
  StatModifier damage;

  /** Modifiction of range.  */
  StatModifier range;

  CombatModifier () = default;
  CombatModifier (CombatModifier&&) = default;

  CombatModifier (const CombatModifier&) = delete;
  void operator= (const CombatModifier&) = delete;

};

/**
 * Computes the low-HP boosts to damage and range for the given entity.
 */
void
ComputeLowHpBoosts (const CombatEntity& f, CombatModifier& mod)
{
  mod.damage = StatModifier ();
  mod.range = StatModifier ();

  const auto& cd = f.GetCombatData ();
  const auto& hp = f.GetHP ();
  const auto& maxHp = f.GetRegenData ().max_hp ();

  for (const auto& b : cd.low_hp_boosts ())
    {
      /* hp / max > p / 100 iff 100 hp > p max */
      if (100 * hp.armour () > b.max_hp_percent () * maxHp.armour ())
        continue;

      mod.damage += b.damage ();
      mod.range += b.range ();
    }
}

} // anonymous namespace

TargetKey::TargetKey (const proto::TargetId& id)
{
  CHECK (id.has_id ());
  first = id.type ();
  second = id.id ();
}

proto::TargetId
TargetKey::ToProto () const
{
  proto::TargetId res;
  res.set_type (first);
  res.set_id (second);
  return res;
}

/* ************************************************************************** */

namespace
{

/**
 * Runs target selection for one fighter entity.
 */
void
SelectTarget (TargetFinder& targets, xaya::Random& rnd, FighterTable::Handle f)
{
  const HexCoord pos = f->GetCombatPosition ();

  HexCoord::IntT range = f->GetAttackRange ();
  if (range == CombatEntity::NO_ATTACKS)
    {
      VLOG (1) << "Fighter at " << pos << " has no attacks";
      return;
    }
  CHECK_GE (range, 0);

  /* Apply the low-HP boost to range (if any).  */
  CombatModifier lowHpMod;
  ComputeLowHpBoosts (*f, lowHpMod);
  range = lowHpMod.range (range);

  HexCoord::IntT closestRange;
  std::vector<proto::TargetId> closestTargets;

  targets.ProcessL1Targets (pos, range, f->GetFaction (),
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
      f->ClearTarget ();
      return;
    }

  const unsigned ind = rnd.NextInt (closestTargets.size ());
  f->SetTarget (closestTargets[ind]);
}

} // anonymous namespace

void
FindCombatTargets (Database& db, xaya::Random& rnd)
{
  BuildingsTable buildings(db);
  CharacterTable characters(db);
  FighterTable fighters(buildings, characters);
  TargetFinder targets(db);

  fighters.ProcessWithAttacks ([&] (FighterTable::Handle f)
    {
      SelectTarget (targets, rnd, std::move (f));
    });
}

/* ************************************************************************** */

namespace
{

/**
 * Helper class to perform the damage-dealing processing step.
 */
class DamageProcessor
{

private:

  DamageLists& dl;
  xaya::Random& rnd;

  BuildingsTable buildings;
  CharacterTable characters;
  FighterTable fighters;
  TargetFinder targets;

  /**
   * Modifiers to combat stats for all fighters that will deal damage.  This
   * is filled in (e.g. from their low-HP boosts) before actual damaging starts,
   * and is used to make the damaging independent of processing order.
   */
  std::map<TargetKey, CombatModifier> modifiers;

  /** The list of kills being built up.  */
  std::set<TargetKey> dead;

  /**
   * Performs a random roll to determine the damage a particular attack does.
   * The min/max damage is modified according to the stats modifier.
   */
  unsigned RollAttackDamage (const proto::Attack& attack,
                             const StatModifier& mod);

  /**
   * Applies a fixed given amount of damage to a given attack target.
   */
  void ApplyDamage (unsigned dmg, const CombatEntity& attacker,
                    CombatEntity& target);

  /**
   * Applies combat effects (non-damage) to a target.
   */
  static void ApplyEffects (const proto::Attack& attack, CombatEntity& target);

  /**
   * Deals damage for one fighter with a target to the respective target
   * (or any AoE targets).
   */
  void DealDamage (FighterTable::Handle f);

public:

  explicit DamageProcessor (Database& db, DamageLists& lst, xaya::Random& r)
    : dl(lst), rnd(r),
      buildings(db), characters(db),
      fighters(buildings, characters),
      targets(db)
  {}

  /**
   * Runs the full damage processing step.
   */
  void Process ();

  /**
   * Returns the list of killed fighters.
   */
  const std::set<TargetKey>&
  GetDead () const
  {
    return dead;
  }

};

unsigned
DamageProcessor::RollAttackDamage (const proto::Attack& attack,
                                   const StatModifier& mod)
{
  CHECK (attack.has_damage ());
  const auto& dmg = attack.damage ();
  const auto minDmg = mod (dmg.min ());
  const auto maxDmg = mod (dmg.max ());

  CHECK_LE (minDmg, maxDmg);
  const auto n = maxDmg - minDmg + 1;
  return minDmg + rnd.NextInt (n);
}

void
DamageProcessor::ApplyDamage (unsigned dmg, const CombatEntity& attacker,
                              CombatEntity& target)
{
  const auto targetId = target.GetIdAsTarget ();
  if (dmg == 0)
    {
      VLOG (1) << "No damage done to target:\n" << targetId.DebugString ();
      return;
    }
  VLOG (1)
      << "Dealing " << dmg << " damage to target:\n" << targetId.DebugString ();

  const auto attackerId = attacker.GetIdAsTarget ();
  if (attackerId.type () == proto::TargetId::TYPE_CHARACTER
        && targetId.type () == proto::TargetId::TYPE_CHARACTER)
    dl.AddEntry (targetId.id (), attackerId.id ());

  auto& hp = target.MutableHP ();

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
      CHECK (dead.insert (targetId).second)
          << "Target is already dead:\n" << targetId.DebugString ();
    }
}

void
DamageProcessor::ApplyEffects (const proto::Attack& attack,
                               CombatEntity& target)
{
  if (!attack.has_effects ())
    return;

  const auto targetId = target.GetIdAsTarget ();
  VLOG (1) << "Applying combat effects to " << targetId.DebugString ();

  const auto& effects = attack.effects ();
  auto& pb = target.MutableEffects ();

  if (effects.has_speed ())
    *pb.mutable_speed () += effects.speed ();
}

void
DamageProcessor::DealDamage (FighterTable::Handle f)
{
  const auto& cd = f->GetCombatData ();
  const auto& pos = f->GetCombatPosition ();

  CHECK (f->HasTarget ());
  FighterTable::Handle tf = fighters.GetForTarget (f->GetTarget ());
  const auto targetPos = tf->GetCombatPosition ();
  const auto targetDist = HexCoord::DistanceL1 (pos, targetPos);
  tf.reset ();

  const auto& mod = modifiers.at (f->GetIdAsTarget ());

  for (const auto& attack : cd.attacks ())
    {
      /* If this is not a centred-on-attacker AoE attack, check that
         the target is actually within range of this attack.  */
      if (attack.has_range ()
            && targetDist > static_cast<int> (mod.range (attack.range ())))
        continue;

      unsigned dmg = 0;
      if (attack.has_damage ())
        dmg = RollAttackDamage (attack, mod.damage);

      if (attack.has_area ())
        {
          HexCoord centre;
          if (attack.has_range ())
            centre = targetPos;
          else
            centre = pos;

          targets.ProcessL1Targets (centre, mod.range (attack.area ()),
                                    f->GetFaction (),
            [&] (const HexCoord& c, const proto::TargetId& id)
            {
              auto t = fighters.GetForTarget (id);
              ApplyDamage (dmg, *f, *t);
              ApplyEffects (attack, *t);
            });
        }
      else
        {
          auto t = fighters.GetForTarget (f->GetTarget ());
          ApplyDamage (dmg, *f, *t);
          ApplyEffects (attack, *t);
        }
    }
}

void
DamageProcessor::Process ()
{
  modifiers.clear ();
  fighters.ProcessWithTarget ([&] (FighterTable::Handle f)
    {
      CombatModifier mod;
      ComputeLowHpBoosts (*f, mod);
      CHECK (modifiers.emplace (f->GetIdAsTarget (), std::move (mod)).second);
    });

  fighters.ProcessWithTarget ([&] (FighterTable::Handle f)
    {
      DealDamage (std::move (f));
    });
}

} // anonymous namespace

std::set<TargetKey>
DealCombatDamage (Database& db, DamageLists& dl, xaya::Random& rnd)
{
  DamageProcessor proc(db, dl, rnd);
  proc.Process ();
  return proc.GetDead ();
}

/* ************************************************************************** */

namespace
{

/**
 * Utility class that handles processing of killed characters and buildings.
 */
class KillProcessor
{

private:

  xaya::Random& rnd;
  const Context& ctx;

  DamageLists& damageLists;
  GroundLootTable& loot;

  BuildingsTable buildings;
  BuildingInventoriesTable inventories;
  CharacterTable characters;
  OngoingsTable ongoings;
  RegionsTable regions;

  /**
   * Deletes a character from the database in all tables.  Takes ownership
   * of and destructs the handle to it.
   */
  void
  DeleteCharacter (CharacterTable::Handle h)
  {
    const auto id = h->GetId ();
    h.reset ();
    damageLists.RemoveCharacter (id);
    ongoings.DeleteForCharacter (id);
    characters.DeleteById (id);
  }

public:

  explicit KillProcessor (Database& db, DamageLists& dl, GroundLootTable& l,
                          xaya::Random& r, const Context& c)
    : rnd(r), ctx(c), damageLists(dl), loot(l),
      buildings(db), inventories(db), characters(db),
      ongoings(db), regions(db, ctx.Height ())
  {}

  KillProcessor () = delete;
  KillProcessor (const KillProcessor&) = delete;
  void operator= (const KillProcessor&) = delete;

  /**
   * Processes everything for a character killed in combat.
   */
  void ProcessCharacter (const Database::IdT id);

  /**
   * Processes everything for a building that has been destroyed.
   */
  void ProcessBuilding (const Database::IdT id);

};

void
KillProcessor::ProcessCharacter (const Database::IdT id)
{
  auto c = characters.GetById (id);
  const auto& pos = c->GetPosition ();

  /* If the character was prospecting some region, cancel that
     operation and mark the region as not being prospected.  */
  if (c->IsBusy ())
    {
      const auto op = ongoings.GetById (c->GetProto ().ongoing ());
      CHECK (op != nullptr);
      if (op->GetProto ().has_prospection ())
        {
          const auto regionId = ctx.Map ().Regions ().GetRegionId (pos);
          LOG (INFO)
              << "Killed character " << id
              << " was prospecting region " << regionId
              << ", cancelling";

          auto r = regions.GetById (regionId);
          CHECK_EQ (r->GetProto ().prospecting_character (), id);
          r->MutableProto ().clear_prospecting_character ();
        }
    }

  /* If the character has an inventory, drop everything they had
     on the ground. */
  const auto& inv = c->GetInventory ();
  if (!inv.IsEmpty ())
    {
      LOG (INFO)
          << "Killed character " << id
          << " has non-empty inventory, dropping loot at " << pos;

      auto ground = loot.GetByCoord (pos);
      auto& groundInv = ground->GetInventory ();
      for (const auto& entry : inv.GetFungible ())
        {
          VLOG (1)
              << "Dropping " << entry.second << " of " << entry.first;
          groundInv.AddFungibleCount (entry.first, entry.second);
        }
    }

  DeleteCharacter (std::move (c));
}

void
KillProcessor::ProcessBuilding (const Database::IdT id)
{
  /* Some of the buildings inventory will be dropped on the floor, so we
     need to compute a "combined inventory" of everything that is inside
     the building (all account inventories in the building plus the
     inventories of all characters inside).

     In addition to that, we destroy all characters inside the building.  */

  Inventory totalInv;

  {
    auto res = inventories.QueryForBuilding (id);
    while (res.Step ())
      totalInv += inventories.GetFromResult (res)->GetInventory ();
  }

  {
    auto res = characters.QueryForBuilding (id);
    while (res.Step ())
      {
        auto h = characters.GetFromResult (res);
        totalInv += h->GetInventory ();
        DeleteCharacter (std::move (h));
      }
  }

  {
    auto res = ongoings.QueryForBuilding (id);
    while (res.Step ())
      {
        auto op = ongoings.GetFromResult (res);

        if (op->GetProto ().has_blueprint_copy ())
          {
            const auto& type
                = op->GetProto ().blueprint_copy ().original_type ();
            totalInv.AddFungibleCount (type, 1);
            continue;
          }

        if (op->GetProto ().has_item_construction ())
          {
            const auto& c = op->GetProto ().item_construction ();
            if (c.has_original_type ())
              totalInv.AddFungibleCount (c.original_type (), 1);
            continue;
          }
      }
  }

  auto b = buildings.GetById (id);
  CHECK (b != nullptr) << "Killed non-existant building " << id;
  if (b->GetProto ().has_construction_inventory ())
    totalInv += Inventory (b->GetProto ().construction_inventory ());

  /* The underlying proto map does not have a well-defined order.  Since the
     random rolls depend on the other, make sure to explicitly sort the
     the list of inventory positions.  */
  const auto& protoInvMap = totalInv.GetFungible ();
  const std::map<std::string, Inventory::QuantityT> invItems (
      protoInvMap.begin (), protoInvMap.end ());

  auto lootHandle = loot.GetByCoord (b->GetCentre ());
  b.reset ();

  for (const auto& entry : invItems)
    {
      CHECK_GT (entry.second, 0);
      if (!rnd.ProbabilityRoll (BUILDING_INVENTORY_DROP_PERCENT, 100))
        {
          VLOG (1)
              << "Not dropping " << entry.second << " " << entry.first
              << " from destroyed building " << id;
          continue;
        }

      VLOG (1)
          << "Dropping " << entry.second << " " << entry.first
          << " from destroyed building " << id
          << " at " << lootHandle->GetPosition ();
      lootHandle->GetInventory ().AddFungibleCount (entry.first, entry.second);
    }

  inventories.RemoveBuilding (id);
  ongoings.DeleteForBuilding (id);
  buildings.DeleteById (id);
}

} // anonymous namespace

void
ProcessKills (Database& db, DamageLists& dl, GroundLootTable& loot,
              const std::set<TargetKey>& dead,
              xaya::Random& rnd, const Context& ctx)
{
  KillProcessor proc(db, dl, loot, rnd, ctx);

  for (const auto& id : dead)
    switch (id.first)
      {
      case proto::TargetId::TYPE_CHARACTER:
        proc.ProcessCharacter (id.second);
        break;

      case proto::TargetId::TYPE_BUILDING:
        proc.ProcessBuilding (id.second);
        break;

      default:
        LOG (FATAL)
            << "Invalid target type killed: " << static_cast<int> (id.first);
      }
}

/* ************************************************************************** */

namespace
{

/**
 * Applies HP regeneration (if any) to a given fighter.
 */
void
RegenerateFighterHP (FighterTable::Handle f)
{
  const auto& regen = f->GetRegenData ();
  const auto& hp = f->GetHP ();

  /* Make sure to return early if there is no regeneration at all.  This
     ensures that we are not doing unnecessary database updates triggered
     through MutableHP().  */
  if (regen.shield_regeneration_mhp () == 0
        || hp.shield () == regen.max_hp ().shield ())
    return;

  unsigned shield = hp.shield ();
  unsigned mhp = hp.shield_mhp ();

  mhp += regen.shield_regeneration_mhp ();
  shield += mhp / 1000;
  mhp %= 1000;

  if (shield >= regen.max_hp ().shield ())
    {
      shield = regen.max_hp ().shield ();
      mhp = 0;
    }

  auto& mutableHP = f->MutableHP ();
  mutableHP.set_shield (shield);
  mutableHP.set_shield_mhp (mhp);
}

} // anonymous namespace

void
RegenerateHP (Database& db)
{
  BuildingsTable buildings(db);
  CharacterTable characters(db);
  FighterTable fighters(buildings, characters);

  fighters.ProcessForRegen ([] (FighterTable::Handle f)
    {
      RegenerateFighterHP (std::move (f));
    });
}

/* ************************************************************************** */

void
AllHpUpdates (Database& db, FameUpdater& fame, xaya::Random& rnd,
              const Context& ctx)
{
  const auto dead = DealCombatDamage (db, fame.GetDamageLists (), rnd);

  for (const auto& id : dead)
    fame.UpdateForKill (id.ToProto ());

  GroundLootTable loot(db);
  ProcessKills (db, fame.GetDamageLists (), loot, dead, rnd, ctx);

  RegenerateHP (db);
}

} // namespace pxd
