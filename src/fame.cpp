/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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
#include <map>
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
  /* A victim loses up to FAME_PER_KILL for each of its characters killed, but
     several kills in the same block split its fame between them.  If each
     took up to FAME_PER_KILL of it, the killers could gain more than the
     victim has (the floor at zero swallowing the difference), so fame could
     be farmed by having many characters of one account killed at once.  */
  std::map<std::string, unsigned> killsOf;
  for (const auto& k : kills)
    ++killsOf[k.victim];

  std::map<std::string, int> deltas;
  for (const auto& k : kills)
    {
      const unsigned fameLost
          = std::min (k.victimFame / killsOf[k.victim], FAME_PER_KILL);
      const unsigned famePerKiller = fameLost / k.numKillers;
      VLOG (1)
          << "Fame lost by " << k.victim << ": " << fameLost
          << ", gained per killer: " << famePerKiller;

      for (const auto& name : k.killers)
        deltas[name] += famePerKiller;
      deltas[k.victim] -= fameLost;
    }

  for (const auto& entry : deltas)
    {
      VLOG (1)
          << "Applying fame delta " << entry.second << " for " << entry.first;

      auto h = accounts.GetByName (entry.first);
      CHECK (h != nullptr);
      int fame = h->GetProto ().fame ();
      fame += entry.second;
      fame = std::min<int> (MAX_FAME, std::max (0, fame));
      h->MutableProto ().set_fame (fame);
    }

  for (const auto& entry : numKills)
    {
      auto h = accounts.GetByName (entry.first);
      CHECK (h != nullptr);
      auto& pb = h->MutableProto ();
      pb.set_kills (pb.kills () + entry.second);
    }
}

const std::string&
FameUpdater::GetOwner (const Database::IdT id)
{
  auto mit = ownerOf.find (id);
  if (mit == ownerOf.end ())
    {
      auto c = characters.GetById (id);
      CHECK (c != nullptr) << "Character " << id << " does not exist";
      mit = ownerOf.emplace (id, c->GetOwner ()).first;
    }

  return mit->second;
}

unsigned
FameUpdater::GetFame (const std::string& name)
{
  auto mit = fameOf.find (name);
  if (mit == fameOf.end ())
    {
      const auto h = accounts.GetByName (name);
      CHECK (h != nullptr) << "Account " << name << " does not exist";
      mit = fameOf.emplace (name, h->GetProto ().fame ()).first;
    }

  return mit->second;
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
  const std::string& victimOwner = GetOwner (victim);
  const unsigned victimFame = GetFame (victimOwner);
  const int victimLevel = GetLevel (victimFame);
  VLOG (1)
      << "Victim fame: " << victimFame << " (level: " << victimLevel << ")";

  /* Find the set of distinct accounts that killed the victim.  Characters of
     the victim's own account (which can damage it with mentecon) are not
     killers of it.  */
  std::set<std::string> owners;
  for (const auto attackerId : attackers)
    {
      const std::string& owner = GetOwner (attackerId);
      if (owner != victimOwner)
        owners.insert (owner);
    }

  /* Process the killer accounts in a first round.  We count the kill for
     them here already, and find the set of killers that are within the
     level range to receive fame.  */
  std::vector<std::string> inRangeKillers;
  for (const auto& owner : owners)
    {
      VLOG (1) << "Killing account: " << owner;
      ++numKills[owner];

      const unsigned fame = GetFame (owner);
      const int level = GetLevel (fame);
      VLOG (1) << "Killer fame: " << fame << " (level: " << level << ")";

      if (std::abs (level - victimLevel) <= 1)
        inRangeKillers.push_back (owner);
    }

  /* The fame of the in-range killers and the victim is updated in the end,
     for all kills together.  */
  VLOG (1) << "We have " << inRangeKillers.size () << " in-range killers";
  if (!inRangeKillers.empty ())
    kills.push_back ({victimOwner, victimFame, std::move (inRangeKillers),
                      static_cast<unsigned> (owners.size ())});
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
