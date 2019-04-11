#include "fame.hpp"

#include <glog/logging.h>

#include <algorithm>
#include <cstdlib>
#include <set>
#include <vector>

namespace pxd
{

namespace
{

/** Maximum value for fame of a player.  */
constexpr unsigned MAX_FAME = 9999;

/** Amount of fame transferred for a kill.  */
constexpr unsigned FAME_PER_KILL = 100;

} // anonymous namespace

int
FameUpdater::GetLevel (const unsigned fame)
{
  const int res = fame / 1000;
  return std::min (res, 8);
}

unsigned
FameUpdater::GetOriginalFame (const Account& a)
{
  const auto mit = originalFame.find (a.GetName ());
  if (mit != originalFame.end ())
    return mit->second;

  const unsigned res = a.GetFame ();
  originalFame.emplace (a.GetName (), res);

  return res;
}

void
FameUpdater::UpdateForKill (const Database::IdT victim,
                            const DamageLists::Attackers& attackers)
{
  VLOG (1) << "Updating fame for killing of character " << victim;

  /* Determine the victim's fame level.  */
  auto victimCharacter = characters.GetById (victim);
  const std::string& victimOwner = victimCharacter->GetOwner ();
  const unsigned victimFame
      = GetOriginalFame (*accounts.GetByName (victimOwner));
  const int victimLevel = GetLevel (victimFame);
  VLOG (1)
      << "Victim fame: " << victimFame << " (level: " << victimLevel << ")";

  /* Find the set of distinct accounts that killed the victim.  */
  std::set<std::string> owners;
  for (const auto attackerId : attackers)
    {
      auto c = characters.GetById (attackerId);
      CHECK (c != nullptr);
      owners.insert (c->GetOwner ());
    }

  /* Process the killer accounts in a first round.  We update the kills counter
     here already, and find the set of killers that are within the level range
     to receive fame.  */
  std::vector<AccountsTable::Handle> inRangeKillers;
  for (const auto& owner : owners)
    {
      VLOG (1) << "Killing account: " << owner;
      auto a = accounts.GetByName (owner);
      a->SetKills (a->GetKills () + 1);

      const unsigned fame = GetOriginalFame (*a);
      const int level = GetLevel (fame);
      VLOG (1) << "Killer fame: " << fame << " (level: " << level << ")";

      if (std::abs (level - victimLevel) <= 1)
        inRangeKillers.push_back (std::move (a));
    }

  /* Actually update the fame for the in-range killers.  */
  VLOG (1) << "We have " << inRangeKillers.size () << " in-range killers";
  if (inRangeKillers.empty ())
    return;

  const unsigned fameLost = std::min (victimFame, FAME_PER_KILL);
  VLOG (1) << "Fame lost: " << fameLost;

  const unsigned famePerKiller = fameLost / owners.size ();
  VLOG (1) << "Fame gained per killer: " << famePerKiller;
  for (auto& entry : inRangeKillers)
    entry->SetFame (std::min (MAX_FAME, entry->GetFame () + famePerKiller));

  /* Finally, update the victim fame itself.  A tricky situation arises if the
     victim is one of the killers as well:  For this situation, we need to make
     sure that all killer fame updates are "flushed" already to the database
     before we update the victim for the losses!  */
  inRangeKillers.clear ();
  auto victimAccount = accounts.GetByName (victimOwner);
  victimAccount->SetFame (victimAccount->GetFame () - fameLost);
}

void
FameUpdater::UpdateForKill (const proto::TargetId& target)
{
  if (target.type () != proto::TargetId::TYPE_CHARACTER)
    return;

  const auto& attackers = dl.GetAttackers (target.id ());
  UpdateForKill (target.id (), attackers);
}

} // namespace pxd
