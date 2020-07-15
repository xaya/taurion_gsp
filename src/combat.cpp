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
 * Computes the modifier to apply for a given entity (composed of low-HP boosts
 * and effects).
 */
void
ComputeModifier (const CombatEntity& f, CombatModifier& mod)
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

  mod.range += f.GetEffects ().range ();
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
SelectTarget (TargetFinder& targets, xaya::Random& rnd, const Context& ctx,
              FighterTable::Handle f)
{
  const HexCoord pos = f->GetCombatPosition ();
  if (ctx.Map ().SafeZones ().IsNoCombat (pos))
    {
      VLOG (1)
          << "Not selecting targets for fighter in no-combat zone:\n"
          << f->GetIdAsTarget ().DebugString ();
      f->ClearTarget ();
      return;
    }

  HexCoord::IntT range = f->GetAttackRange ();
  if (range == CombatEntity::NO_ATTACKS)
    {
      VLOG (1) << "Fighter at " << pos << " has no attacks";
      return;
    }
  CHECK_GE (range, 0);

  /* Apply the modifier to range (if any).  */
  {
    CombatModifier mod;
    ComputeModifier (*f, mod);
    range = mod.range (range);
  }

  HexCoord::IntT closestRange;
  std::vector<proto::TargetId> closestTargets;

  targets.ProcessL1Targets (pos, range, f->GetFaction (), true, false,
    [&] (const HexCoord& c, const proto::TargetId& id)
    {
      if (ctx.Map ().SafeZones ().IsNoCombat (c))
        {
          VLOG (2)
              << "Ignoring fighter in no-combat zone for target selection:\n"
              << id.DebugString ();
          return;
        }

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
FindCombatTargets (Database& db, xaya::Random& rnd, const Context& ctx)
{
  BuildingsTable buildings(db);
  CharacterTable characters(db);
  FighterTable fighters(buildings, characters);
  TargetFinder targets(db);

  fighters.ProcessWithAttacks ([&] (FighterTable::Handle f)
    {
      SelectTarget (targets, rnd, ctx, std::move (f));
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
  const Context& ctx;

  BuildingsTable buildings;
  CharacterTable characters;
  FighterTable fighters;
  TargetFinder targets;

  /**
   * Modifiers to combat stats for all fighters that will deal damage.  This
   * is filled in (e.g. from their low-HP boosts) before actual damaging starts,
   * and is used to make the damaging independent of processing order.  This is
   * especially important so that HP changes do not influence low-HP boosts.
   */
  std::map<TargetKey, CombatModifier> modifiers;

  /**
   * Combat effects that are being applied by this round of damage to
   * the given targets.  This is accumulated here so that the original
   * effects are unaffected, and only later written back to the fighters
   * after all damaging is done.  This ensures that we do not take current
   * changes into effect right now in a messy way, e.g. for self-destruct
   * rounds (which do not rely on "modifiers" but recompute them).
   */
  std::map<TargetKey, proto::CombatEffects> newEffects;

  /**
   * For each target that was attacked with a gain_hp attack, we store all
   * attackers and how many HP they drained.  We give them those HP back
   * only later, after processing all damage and kills (i.e. HP you gained
   * in one round do not prevent you from dying in that round).  Also, if
   * a single target was drained by more than one attacker and ends up with
   * no HP left, noone gets any of them.
   *
   * DealDamage fills this in whenever it processes an attack that has
   * gain_hp set.
   *
   * This system ensures that processing is independent of the order in which
   * the individual attackers are handled; if two people drained the same
   * target and it ends up without HP (so that the order might have mattered),
   * then noone gets any.
   */
  std::map<TargetKey, std::map<TargetKey, proto::HP>> gainHpDrained;

  /**
   * The list of dead targets.  We use this to avoid giving out fame for
   * kills of already-dead targets in later rounds of self-destruct.  The list
   * being built up during a round of damage is a temporary, that gets put
   * here only after the round.
   */
  std::set<TargetKey> alreadyDead;

  /**
   * Performs a random roll to determine the damage a particular attack does.
   * The min/max damage is modified according to the stats modifier.
   */
  unsigned RollAttackDamage (const proto::Attack::Damage& attack,
                             const StatModifier& mod);

  /**
   * Applies a fixed given amount of damage to a given attack target.  Adds
   * the target into newDead if it is now dead.  This is a more low-level
   * variant that does not handle gain_hp.  Returns the damage actually
   * done to the target's shield and armour.
   */
  proto::HP ApplyDamage (unsigned dmg, const CombatEntity& attacker,
                         const proto::Attack::Damage& pb,
                         CombatEntity& target, std::set<TargetKey>& newDead);

  /**
   * Applies a fixed amount of damage to a given target.  This is the
   * high-level variant that also handles gain_hp and is used for real attacks,
   * but not self-destruct damage.
   */
  void ApplyDamage (unsigned dmg, const CombatEntity& attacker,
                    const proto::Attack& attack,
                    CombatEntity& target, std::set<TargetKey>& newDead);

  /**
   * Applies combat effects (non-damage) to a target.  They are not saved
   * directly to the target for now, but accumulated in newEffects.
   */
  void ApplyEffects (const proto::Attack& attack, const CombatEntity& target);

  /**
   * Deals damage for one fighter with a target to the respective target
   * (or any AoE targets).  Only processes attacks with gain_hp equal to
   * the argument value passed in.
   */
  void DealDamage (FighterTable::Handle f, bool forGainHp,
                   std::set<TargetKey>& newDead);

  /**
   * Processes all damage the given fighter does due to self-destruct
   * abilities when killed.
   */
  void ProcessSelfDestructs (FighterTable::Handle f,
                             std::set<TargetKey>& newDead);

public:

  explicit DamageProcessor (Database& db, DamageLists& lst,
                            xaya::Random& r, const Context& c)
    : dl(lst), rnd(r), ctx(c),
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
    return alreadyDead;
  }

};

unsigned
DamageProcessor::RollAttackDamage (const proto::Attack::Damage& dmg,
                                   const StatModifier& mod)
{
  const auto minDmg = mod (dmg.min ());
  const auto maxDmg = mod (dmg.max ());

  CHECK_LE (minDmg, maxDmg);
  const auto n = maxDmg - minDmg + 1;
  return minDmg + rnd.NextInt (n);
}

namespace
{

/**
 * Computes the damage done vs shield and armour, given the total
 * damage roll and the remaining shield and armour of the target.
 * The Damage proto is used for the shield/armour damage percentages
 * (if there are any).
 */
proto::HP
ComputeDamage (unsigned dmg, const proto::Attack::Damage& pb,
               const proto::HP& hp)
{
  proto::HP done;

  const unsigned shieldPercent
      = (pb.has_shield_percent () ? pb.shield_percent () : 100);
  const unsigned armourPercent
      = (pb.has_armour_percent () ? pb.armour_percent () : 100);

  /* To take the shield vs armour percentages into account, we first
     multiply the base damage with the corresponding fraction, then deduct
     it from the shield, and then divide the damage done by the fraction again
     to determine how much base damage (if any) is left to apply to the armour.
     There we do the same.

     In the integer math, we always round down; this ensures that we will
     never get more than the original base damage as "damage done".  */

  const unsigned availableForShield = (dmg * shieldPercent) / 100;
  done.set_shield (std::min (availableForShield, hp.shield ()));

  /* If we did not yet exhaust the shield, do not try to damage the armour
     even if some "base damage" is left.  This can happen for instance if
     the shield damage was discounted heavily by the shield percent.  */
  CHECK_LE (done.shield (), hp.shield ());
  if (done.shield () < hp.shield ())
    return done;

  if (done.shield () > 0)
    {
      const unsigned baseDoneShield = (done.shield () * 100) / shieldPercent;
      CHECK_LE (baseDoneShield, dmg);
      dmg -= baseDoneShield;
    }

  const unsigned availableForArmour = (dmg * armourPercent) / 100;
  done.set_armour (std::min (availableForArmour, hp.armour ()));

  if (done.armour () > 0)
    {
      const unsigned baseDoneArmour = (done.armour () * 100) / armourPercent;
      CHECK_LE (baseDoneArmour, dmg);
    }

  return done;
}

} // anonymous namespace

proto::HP
DamageProcessor::ApplyDamage (unsigned dmg, const CombatEntity& attacker,
                              const proto::Attack::Damage& pb,
                              CombatEntity& target,
                              std::set<TargetKey>& newDead)
{
  CHECK (!ctx.Map ().SafeZones ().IsNoCombat (target.GetCombatPosition ()));

  const auto targetId = target.GetIdAsTarget ();
  const auto& targetData = target.GetCombatData ();
  const StatModifier recvDamage(targetData.received_damage_modifier ());
  const auto updatedDamage = recvDamage (dmg);
  CHECK_GE (updatedDamage, 0);
  if (updatedDamage != dmg)
    {
      VLOG (1)
          << "Damage modifier for " << targetId.DebugString ()
          << " changed " << dmg << " to " << updatedDamage;
      dmg = updatedDamage;
    }

  /* Handle cases when we exit early and don't even account for the attack
     in the damage lists:  No damage done at all (e.g. after modifier)
     and the target is already dead from a previous round of self-destructs
     or attacks.  */
  if (dmg == 0)
    {
      VLOG (1) << "No damage done to target:\n" << targetId.DebugString ();
      return proto::HP ();
    }
  const TargetKey targetKey(targetId);
  if (alreadyDead.count (targetKey) > 0)
    {
      VLOG (1)
          << "Target is already dead from before:\n" << targetId.DebugString ();
      return proto::HP ();
    }
  VLOG (1)
      << "Dealing " << dmg << " damage to target:\n" << targetId.DebugString ();

  const auto attackerId = attacker.GetIdAsTarget ();
  if (attackerId.type () == proto::TargetId::TYPE_CHARACTER
        && targetId.type () == proto::TargetId::TYPE_CHARACTER)
    dl.AddEntry (targetId.id (), attackerId.id ());

  auto& hp = target.MutableHP ();
  const auto done = ComputeDamage (dmg, pb, hp);

  hp.set_shield (hp.shield () - done.shield ());
  hp.set_armour (hp.armour () - done.armour ());

  VLOG (1) << "Total damage done: " << (done.shield () + done.armour ());
  VLOG (1) << "Remaining total HP: " << (hp.armour () + hp.shield ());
  if (done.shield () + done.armour () > 0 && hp.armour () + hp.shield () == 0)
    {
      /* Regenerated partial HP are ignored (i.e. you die even with 999/1000
         partial HP).  Just make sure that the partial HP are not full yet
         due to some bug.  */
      CHECK_LT (hp.mhp ().shield (), 1'000);
      CHECK_LT (hp.mhp ().armour (), 1'000);
      CHECK (newDead.insert (targetKey).second)
          << "Target is already dead:\n" << targetId.DebugString ();
    }

  return done;
}

void
DamageProcessor::ApplyDamage (const unsigned dmg, const CombatEntity& attacker,
                              const proto::Attack& attack,
                              CombatEntity& target,
                              std::set<TargetKey>& newDead)
{
  const auto done
      = ApplyDamage (dmg, attacker, attack.damage (), target, newDead);

  /* If this is a gain_hp attack, record the drained HP in the map of
     drain attacks done so we can later process the potential HP gains
     for the attackers.  */
  if (attack.gain_hp ())
    {
      const TargetKey targetId(target.GetIdAsTarget ());
      const TargetKey attackerId(attacker.GetIdAsTarget ());

      auto& drained = gainHpDrained[targetId][attackerId];
      drained.set_armour (drained.armour () + done.armour ());
      drained.set_shield (drained.shield () + done.shield ());
    }
}

void
DamageProcessor::ApplyEffects (const proto::Attack& attack,
                               const CombatEntity& target)
{
  CHECK (!ctx.Map ().SafeZones ().IsNoCombat (target.GetCombatPosition ()));

  if (!attack.has_effects ())
    return;

  const auto targetId = target.GetIdAsTarget ();
  VLOG (1) << "Applying combat effects to " << targetId.DebugString ();

  const auto& attackEffects = attack.effects ();
  auto& targetEffects = newEffects[targetId];

  if (attackEffects.has_speed ())
    *targetEffects.mutable_speed () += attackEffects.speed ();
  if (attackEffects.has_range ())
    *targetEffects.mutable_range () += attackEffects.range ();
}

void
DamageProcessor::DealDamage (FighterTable::Handle f, const bool forGainHp,
                             std::set<TargetKey>& newDead)
{
  const auto& cd = f->GetCombatData ();
  const auto& pos = f->GetCombatPosition ();
  CHECK (!ctx.Map ().SafeZones ().IsNoCombat (pos));

  CHECK (f->HasTarget ());
  FighterTable::Handle tf = fighters.GetForTarget (f->GetTarget ());
  const auto targetPos = tf->GetCombatPosition ();
  const auto targetDist = HexCoord::DistanceL1 (pos, targetPos);
  tf.reset ();

  const auto& mod = modifiers.at (f->GetIdAsTarget ());

  for (const auto& attack : cd.attacks ())
    {
      if (attack.gain_hp () != forGainHp)
        continue;

      /* If this is not a centred-on-attacker AoE attack, check that
         the target is actually within range of this attack.  */
      if (attack.has_range ()
            && targetDist > static_cast<int> (mod.range (attack.range ())))
        continue;

      unsigned dmg = 0;
      if (attack.has_damage ())
        dmg = RollAttackDamage (attack.damage (), mod.damage);

      if (attack.has_area ())
        {
          HexCoord centre;
          if (attack.has_range ())
            centre = targetPos;
          else
            centre = pos;

          targets.ProcessL1Targets (centre, mod.range (attack.area ()),
                                    f->GetFaction (), true, false,
            [&] (const HexCoord& c, const proto::TargetId& id)
            {
              auto t = fighters.GetForTarget (id);
              if (ctx.Map ().SafeZones ().IsNoCombat (t->GetCombatPosition ()))
                {
                  VLOG (2)
                      << "No AoE damage to fighter in safe zone:\n"
                      << t->GetIdAsTarget ().DebugString ();
                  return;
                }
              ApplyDamage (dmg, *f, attack, *t, newDead);
              ApplyEffects (attack, *t);
            });
        }
      else
        {
          auto t = fighters.GetForTarget (f->GetTarget ());
          ApplyDamage (dmg, *f, attack, *t, newDead);
          ApplyEffects (attack, *t);
        }
    }
}

void
DamageProcessor::ProcessSelfDestructs (FighterTable::Handle f,
                                       std::set<TargetKey>& newDead)
{
  const auto& pos = f->GetCombatPosition ();
  CHECK (!ctx.Map ().SafeZones ().IsNoCombat (pos));

  /* The killed fighter should have zero HP left, and thus also should get
     all low-HP boosts now.  */
  CHECK_EQ (f->GetHP ().armour (), 0);
  CHECK_EQ (f->GetHP ().shield (), 0);
  CombatModifier mod;
  ComputeModifier (*f, mod);

  for (const auto& sd : f->GetCombatData ().self_destructs ())
    {
      const auto dmg = RollAttackDamage (sd.damage (), mod.damage);
      VLOG (1)
          << "Dealing " << dmg
          << " of damage for self-destruct of "
          << f->GetIdAsTarget ().DebugString ();

      targets.ProcessL1Targets (pos, mod.range (sd.area ()),
                                f->GetFaction (), true, false,
        [&] (const HexCoord& c, const proto::TargetId& id)
        {
          auto t = fighters.GetForTarget (id);
          if (ctx.Map ().SafeZones ().IsNoCombat (t->GetCombatPosition ()))
            {
              VLOG (2)
                  << "No self-destruct damage to fighter in safe zone:\n"
                  << t->GetIdAsTarget ().DebugString ();
              return;
            }
          ApplyDamage (dmg, *f, sd.damage (), *t, newDead);
        });
    }
}

void
DamageProcessor::Process ()
{
  modifiers.clear ();
  fighters.ProcessWithTarget ([&] (FighterTable::Handle f)
    {
      CombatModifier mod;
      ComputeModifier (*f, mod);
      CHECK (modifiers.emplace (f->GetIdAsTarget (), std::move (mod)).second);
    });

  std::set<TargetKey> newDead;

  /* We first process all attacks with gain_hp, and only later all without.
     This ensures that normal attacks against shields do not remove the HP
     first before they can be drained by a syphon.  */
  fighters.ProcessWithTarget ([&] (FighterTable::Handle f)
    {
      DealDamage (std::move (f), true, newDead);
    });

  /* Reconcile the set of HP gained by attackers now (before normal attacks
     may bring shields down to zero when they aren't yet, for instance).  */
  std::map<TargetKey, proto::HP> gainedHp;
  for (const auto& targetEntry : gainHpDrained)
    {
      CHECK (!targetEntry.second.empty ());

      const auto tf = fighters.GetForTarget (targetEntry.first.ToProto ());
      const auto& tHp = tf->GetHP ();

      for (const auto& attackEntry : targetEntry.second)
        {
          /* While most of the code here is written to support both armour
             and shield drains, we only actually need shield in the game
             (for the syphon fitment).  Supporting both types also leads to
             more issues with processing order, as the order may e.g. determine
             the split between shield and armour for a general attack.
             Thus we disallow this for simplicity (but we could probably
             work out some rules that make it work).  */
          CHECK_EQ (attackEntry.second.armour (), 0)
              << "Armour drain is not supported";
          CHECK_GT (attackEntry.second.shield (), 0);

          proto::HP gained;

          /* The attacker only gains HP if either noone else drained the
             target in question, or there are HP left (so everyone can indeed
             get what they drained).  */
          if (tHp.armour () > 0 || targetEntry.second.size () == 1)
            gained.set_armour (attackEntry.second.armour ());
          if (tHp.shield () > 0 || targetEntry.second.size () == 1)
            gained.set_shield (attackEntry.second.shield ());

          if (gained.armour () > 0 || gained.shield () > 0)
            {
              auto& gainedEntry = gainedHp[attackEntry.first];
              gainedEntry.set_armour (gainedEntry.armour () + gained.armour ());
              gainedEntry.set_shield (gainedEntry.shield () + gained.shield ());
              VLOG (2)
                  << "Fighter " << attackEntry.first.ToProto ().DebugString ()
                  << " gained HP from "
                  << targetEntry.first.ToProto ().DebugString ()
                  << ":\n" << gained.DebugString ();
            }
        }
    }

  fighters.ProcessWithTarget ([&] (FighterTable::Handle f)
    {
      DealDamage (std::move (f), false, newDead);
    });

  /* After applying the base damage, we process all self-destruct actions
     of kills.  This may lead to more damage and more kills, so we have
     to process as many "rounds" of self-destructs as necessary before
     no new kills are added.  */
  while (!newDead.empty ())
    {
      /* The way we merge in the new elements here is not optimal, as one
         could use a proper merge of sorted ranges instead.  But it is quite
         straight-forward and easy to read, and most likely not performance
         critical anyway.  */
      for (const auto& n : newDead)
        CHECK (alreadyDead.insert (n).second)
            << "Target was already dead before:\n"
            << n.ToProto ().DebugString ();

      const auto toProcess = std::move (newDead);
      CHECK (newDead.empty ());

      for (const auto& d : toProcess)
        ProcessSelfDestructs (fighters.GetForTarget (d.ToProto ()), newDead);
    }

  /* Credit gained HP to everyone who is not dead.  */
  for (const auto& entry : gainedHp)
    {
      if (alreadyDead.count (entry.first) > 0)
        {
          VLOG (1)
              << "Fighter " << entry.first.ToProto ().DebugString ()
              << " was killed, not crediting gained HP";
          continue;
        }

      VLOG (1)
          << "Fighter " << entry.first.ToProto ().DebugString ()
          << " gained HP:\n" << entry.second.DebugString ();

      const auto f = fighters.GetForTarget (entry.first.ToProto ());
      const auto& maxHp = f->GetRegenData ().max_hp ();
      auto& hp = f->MutableHP ();
      hp.set_armour (std::min (hp.armour () + entry.second.armour (),
                               maxHp.armour ()));
      hp.set_shield (std::min (hp.shield () + entry.second.shield (),
                               maxHp.shield ()));
    }

  /* Update combat effects on fighters (clear all previous effects in the
     database, and put back in those that are accumulated in newEffects).

     Conceptually, target finding, waiting for the new block, and then
     applying damaging is "one thing".  Swapping over the effects is done
     here, so it is right after that whole "combat block" for the rest
     of processing (e.g. movement or regeneration) and also the next
     combat block.  */
  fighters.ClearAllEffects ();
  for (auto& entry : newEffects)
    {
      auto f = fighters.GetForTarget (entry.first.ToProto ());
      f->MutableEffects () = std::move (entry.second);
    }
}

} // anonymous namespace

std::set<TargetKey>
DealCombatDamage (Database& db, DamageLists& dl,
                  xaya::Random& rnd, const Context& ctx)
{
  DamageProcessor proc(db, dl, rnd, ctx);
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
  const std::map<std::string, Quantity> invItems (protoInvMap.begin (),
                                                  protoInvMap.end ());

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
 * Performs the regeneration logic for one type of HP (armour or shield).
 * Returns the new "full" and "milli" HP value in the output variables,
 * and true iff something changed.
 */
bool
RegenerateHpType (const unsigned max, const unsigned mhpRate,
                  const unsigned oldCur, const unsigned oldMilli,
                  unsigned& newCur, unsigned& newMilli)
{
  CHECK (oldCur < max || (oldCur == max && oldMilli == 0));

  newMilli = oldMilli + mhpRate;
  newCur = oldCur + newMilli / 1'000;
  newMilli %= 1'000;

  if (newCur >= max)
    {
      newCur = max;
      newMilli = 0;
    }

  CHECK (newCur > oldCur || (newCur == oldCur && newMilli >= oldMilli));
  return newCur != oldCur || newMilli != oldMilli;
}

/**
 * Applies HP regeneration (if any) to a given fighter.
 */
void
RegenerateFighterHP (FighterTable::Handle f)
{
  const auto& regen = f->GetRegenData ();
  const auto& hp = f->GetHP ();

  unsigned cur, milli;

  if (RegenerateHpType (
          regen.max_hp ().armour (), regen.regeneration_mhp ().armour (),
          hp.armour (), hp.mhp ().armour (), cur, milli))
    {
      f->MutableHP ().set_armour (cur);
      f->MutableHP ().mutable_mhp ()->set_armour (milli);
    }

  const StatModifier shieldRegenMod(f->GetEffects ().shield_regen ());
  const unsigned shieldRate
      = shieldRegenMod (regen.regeneration_mhp ().shield ());

  if (RegenerateHpType (
          regen.max_hp ().shield (), shieldRate,
          hp.shield (), hp.mhp ().shield (), cur, milli))
    {
      f->MutableHP ().set_shield (cur);
      f->MutableHP ().mutable_mhp ()->set_shield (milli);
    }
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
  const auto dead = DealCombatDamage (db, fame.GetDamageLists (), rnd, ctx);

  for (const auto& id : dead)
    fame.UpdateForKill (id.ToProto ());

  GroundLootTable loot(db);
  ProcessKills (db, fame.GetDamageLists (), loot, dead, rnd, ctx);

  RegenerateHP (db);
}

} // namespace pxd
