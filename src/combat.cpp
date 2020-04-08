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

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/fighter.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"
#include "database/target.hpp"
#include "hexagonal/coord.hpp"

#include <algorithm>
#include <map>

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
 * Runs target selection for one fighter entity.
 */
void
SelectTarget (TargetFinder& targets, xaya::Random& rnd, FighterTable::Handle f)
{
  const HexCoord pos = f->GetCombatPosition ();

  const HexCoord::IntT range = f->GetAttackRange ();
  if (range == 0)
    {
      VLOG (1) << "Fighter at " << pos << " has no attacks";
      return;
    }
  CHECK_GT (range, 0);

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

namespace
{

/**
 * Performs a random roll to determine the damage a particular attack does.
 */
unsigned
RollAttackDamage (const proto::Attack& attack, xaya::Random& rnd)
{
  CHECK_LE (attack.min_damage (), attack.max_damage ());
  const auto n = attack.max_damage () - attack.min_damage () + 1;
  return attack.min_damage () + rnd.NextInt (n);
}

/**
 * Applies a fixed given amount of damage to a given attack target.
 */
void
ApplyDamage (DamageLists& dl, unsigned dmg,
             const CombatEntity& attacker, FighterTable::Handle target,
             std::vector<proto::TargetId>& dead)
{
  const auto targetId = target->GetIdAsTarget ();
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

  auto& hp = target->MutableHP ();

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
      dead.push_back (targetId);
    }
}

/**
 * Deals damage for one fighter with a target to the respective target.
 * Adds the target to the vector of dead fighters if it had its HP reduced
 * to zero and is now dead.
 */
void
DealDamage (FighterTable& fighters, TargetFinder& targets,
            DamageLists& dl, xaya::Random& rnd,
            FighterTable::Handle f, std::vector<proto::TargetId>& dead)
{
  const auto& cd = f->GetCombatData ();
  const auto& pos = f->GetCombatPosition ();

  /* First, apply all non-area attacks to the selected target.  */
  {
    auto tf = fighters.GetForTarget (f->GetTarget ());
    const auto dist = HexCoord::DistanceL1 (pos, tf->GetCombatPosition ());
    unsigned dmg = 0;
    for (const auto& attack : cd.attacks ())
      {
        if (attack.area ())
          continue;
        if (dist > static_cast<int> (attack.range ()))
          continue;

        dmg += RollAttackDamage (attack, rnd);
      }
    ApplyDamage (dl, dmg, *f, std::move (tf), dead);
  }

  /* Second, apply all area attacks to matching targets.  */
  for (const auto& attack : cd.attacks ())
    {
      if (!attack.area ())
        continue;

      const unsigned dmg = RollAttackDamage (attack, rnd);

      targets.ProcessL1Targets (pos, attack.range (), f->GetFaction (),
        [&] (const HexCoord& c, const proto::TargetId& id)
        {
          ApplyDamage (dl, dmg, *f, fighters.GetForTarget (id), dead);
        });
    }
}

} // anonymous namespace

std::vector<proto::TargetId>
DealCombatDamage (Database& db, DamageLists& dl, xaya::Random& rnd)
{
  BuildingsTable buildings(db);
  CharacterTable characters(db);
  FighterTable fighters(buildings, characters);
  TargetFinder targets(db);

  std::vector<proto::TargetId> dead;
  fighters.ProcessWithTarget ([&] (FighterTable::Handle f)
    {
      DealDamage (fighters, targets, dl, rnd, std::move (f), dead);
    });

  return dead;
}

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

        if (op->GetProto ().has_construction ())
          {
            const auto& c = op->GetProto ().construction ();
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
              const std::vector<proto::TargetId>& dead,
              xaya::Random& rnd, const Context& ctx)
{
  KillProcessor proc(db, dl, loot, rnd, ctx);

  for (const auto& id : dead)
    switch (id.type ())
      {
      case proto::TargetId::TYPE_CHARACTER:
        proc.ProcessCharacter (id.id ());
        break;

      case proto::TargetId::TYPE_BUILDING:
        proc.ProcessBuilding (id.id ());
        break;

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

void
AllHpUpdates (Database& db, FameUpdater& fame, xaya::Random& rnd,
              const Context& ctx)
{
  const auto dead = DealCombatDamage (db, fame.GetDamageLists (), rnd);

  for (const auto& id : dead)
    fame.UpdateForKill (id);

  GroundLootTable loot(db);
  ProcessKills (db, fame.GetDamageLists (), loot, dead, rnd, ctx);

  RegenerateHP (db);
}

} // namespace pxd
