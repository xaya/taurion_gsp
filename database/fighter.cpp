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

#include "fighter.hpp"

#include <glog/logging.h>

namespace pxd
{

proto::TargetId
Fighter::GetId () const
{
  CHECK (character != nullptr);

  proto::TargetId res;
  res.set_type (proto::TargetId::TYPE_CHARACTER);
  res.set_id (character->GetId ());

  return res;
}

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

const proto::RegenData&
Fighter::GetRegenData () const
{
  CHECK (character != nullptr);
  return character->GetRegenData ();
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

HexCoord::IntT
Fighter::GetAttackRange () const
{
  CHECK (character != nullptr);
  return character->GetAttackRange ();
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
  if (character->HasTarget ())
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
FighterTable::ProcessWithAttacks (const Callback& cb)
{
  auto res = characters.QueryWithAttacks ();
  while (res.Step ())
    cb (Fighter (characters.GetFromResult (res)));
}

void
FighterTable::ProcessForRegen (const Callback& cb)
{
  auto res = characters.QueryForRegen ();
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

HexCoord::IntT
FindAttackRange (const proto::CombatData& cd)
{
  HexCoord::IntT res = 0;
  for (const auto& attack : cd.attacks ())
    {
      CHECK_GT (attack.range (), 0);
      res = std::max<HexCoord::IntT> (res, attack.range ());
    }

  return res;
}

} // namespace pxd
