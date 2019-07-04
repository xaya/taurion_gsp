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

FameUpdater::~FameUpdater ()
{
  for (const auto& entry : deltas)
    {
      VLOG (1)
          << "Applying fame delta " << entry.second << " for " << entry.first;

      auto h = accounts.GetByName (entry.first);
      int fame = h->GetFame ();
      fame += entry.second;
      fame = std::min<int> (MAX_FAME, std::max (0, fame));
      h->SetFame (fame);
    }
}

int
FameUpdater::GetLevel (const unsigned fame)
{
  const int res = fame / 1000;
  return std::min (res, 8);
}

void
FameUpdater::UpdateForKill (const Database::IdT victim,
                            const DamageLists::Attackers& attackers)
{
  VLOG (1) << "Updating fame for killing of character " << victim;

  /* Determine the victim's fame level.  */
  auto victimCharacter = characters.GetById (victim);
  const std::string& victimOwner = victimCharacter->GetOwner ();
  const unsigned victimFame = accounts.GetByName (victimOwner)->GetFame ();
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

      const unsigned fame = a->GetFame ();
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
    deltas[entry->GetName ()] += famePerKiller;

  /* Finally, update the victim fame itself.  */
  deltas[victimOwner] -= fameLost;
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
