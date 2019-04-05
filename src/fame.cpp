#include "fame.hpp"

#include <glog/logging.h>

#include <set>
#include <string>

namespace pxd
{

void
FameUpdater::UpdateForKill (const Database::IdT victim,
                            const DamageLists::Attackers& attackers)
{
  VLOG (1) << "Updating fame for killing of character " << victim;

  std::set<std::string> owners;
  for (const auto attackerId : attackers)
    {
      auto c = characters.GetById (attackerId);
      CHECK (c != nullptr);
      owners.insert (c->GetOwner ());
    }

  for (const auto& owner : owners)
    {
      VLOG (1) << "Killing account: " << owner;
      auto a = accounts.GetByName (owner);
      a->SetKills (a->GetKills () + 1);
    }
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
