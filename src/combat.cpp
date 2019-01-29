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

  fighters.ProcessAll ([&targets, &rnd] (Fighter f)
    {
      SelectTarget (targets, rnd, std::move (f));
    });
}

} // namespace pxd
