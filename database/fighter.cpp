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

const proto::CombatData&
Fighter::GetCombatData () const
{
  CHECK (character != nullptr);
  const auto& pb = character->GetProto ();

  /* Every character must have combat data to be valid.  This is set when
     first created and then only updated.  Enforce this requirement here,
     so that we do not accidentally work with an empty proto just because it
     has not been initialised due to a bug.  */
  CHECK (pb.has_combat_data ());

  return pb.combat_data ();
}

const proto::TargetId&
Fighter::GetTarget () const
{
  CHECK (character != nullptr);
  const auto& pb = character->GetProto ();
  CHECK (pb.has_target ());
  return pb.target ();
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

const proto::HP&
Fighter::GetHP () const
{
  CHECK (character != nullptr);
  return character->GetHP ();
}

proto::HP&
Fighter::MutableHP ()
{
  CHECK (character != nullptr);
  return character->MutableHP ();
}

void
Fighter::reset ()
{
  character.reset ();
}

bool
Fighter::empty () const
{
  return character == nullptr;
}

Fighter
FighterTable::GetForTarget (const proto::TargetId& id)
{
  switch (id.type ())
    {
    case proto::TargetId::TYPE_CHARACTER:
      return Fighter (characters.GetById (id.id ()));

    default:
      LOG (FATAL) << "Invalid target type: " << static_cast<int> (id.type ());
    }
}

void
FighterTable::ProcessAll (const Callback& cb)
{
  auto res = characters.QueryAll ();
  while (res.Step ())
    cb (Fighter (characters.GetFromResult (res)));
}

void
FighterTable::ProcessWithTarget (const Callback& cb)
{
  auto res = characters.QueryWithTarget ();
  while (res.Step ())
    cb (Fighter (characters.GetFromResult (res)));
}

} // namespace pxd
