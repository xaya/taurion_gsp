#include "combat.hpp"

#include "database/character.hpp"
#include "database/fighter.hpp"
#include "database/target.hpp"
#include "hexagonal/coord.hpp"

#include <algorithm>
#include <vector>

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
 */
void
DealDamage (FighterTable& fighters, xaya::Random& rnd, Fighter f)
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
      dmg += 1 + rnd.NextInt (attack.max_damage ());
    }

  if (dmg == 0)
    {
      VLOG (1) << "No damage done to target:\n" << target.DebugString ();
      return;
    }
  VLOG (1)
      << "Dealing " << dmg << " damage to target:\n" << target.DebugString ();

  auto& hp = tf.MutableHP ();

  const unsigned shieldDmg = std::min (dmg, hp.shield ());
  hp.set_shield (hp.shield () - shieldDmg);
  dmg -= shieldDmg;

  const unsigned armourDmg = std::min (dmg, hp.armour ());
  hp.set_armour (hp.armour () - armourDmg);
  dmg -= armourDmg;
}

} // anonymous namespace

void
DealCombatDamage (Database& db, xaya::Random& rnd)
{
  CharacterTable characters(db);
  FighterTable fighters(characters);

  fighters.ProcessWithTarget ([&] (Fighter f)
    {
      DealDamage (fighters, rnd, std::move (f));
    });

  /* FIXME: Process also dead characters.  */
}

} // namespace pxd
