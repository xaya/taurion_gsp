#include "fighter.hpp"

#include <glog/logging.h>

namespace pxd
{

Faction
Fighter::GetFaction () const
{
  CHECK (character != nullptr);
  return character->GetFaction ();
}

const HexCoord&
Fighter::GetPosition () const
{
  CHECK (character != nullptr);
  return character->GetPosition ();
}

HexCoord::IntT
Fighter::GetRange () const
{
  /* FIXME: Some hardcoded dummy value for now.  Has to be adapted to a real
     value once we have proper attacks / weapons somewhere in the proto.  */
  return 10;
}

void
Fighter::SetTarget (const proto::TargetId& target)
{
  CHECK (character != nullptr);
  *character->MutableProto ().mutable_target () = target;
}

void
Fighter::ClearTarget ()
{
  CHECK (character != nullptr);

  /* Make sure to mark the proto as dirty only if there was actually a target
     before.  This avoids updating the proto in the database unnecessarily
     for the common case where there was no target and there also is none
     in the future.  */
  if (character->GetProto ().has_target ())
    character->MutableProto ().clear_target ();
}

void
FighterTable::ProcessAll (const Callback& cb)
{
  auto res = characters.QueryAll ();
  while (res.Step ())
    cb (Fighter (characters.GetFromResult (res)));
}

} // namespace pxd
