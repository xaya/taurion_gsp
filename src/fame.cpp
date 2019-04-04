#include "fame.hpp"

#include <glog/logging.h>

namespace pxd
{

void
FameUpdater::UpdateForKill (const Database::IdT victim,
                            const DamageLists::Attackers& attackers)
{
  LOG (WARNING) << "Fame update not yet implemented";
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
