/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "combat.hpp"

#include <glog/logging.h>

namespace pxd
{

CombatEntity::CombatEntity (Database& d)
  : db(d), isNew(true), oldCanRegen(false)
{
  hp.SetToDefault ();
  regenData.SetToDefault ();
  target.SetToDefault ();
}

void
CombatEntity::BindFullFields (Database::Statement& stmt,
                              const unsigned indRegenData,
                              const unsigned indTarget,
                              const unsigned indAttackRange) const
{
  stmt.BindProto (indRegenData, regenData);
  stmt.Bind (indAttackRange, FindAttackRange (GetCombatData ()));

  if (target.Get ().has_id ())
    stmt.BindProto (indTarget, target);
  else
    stmt.BindNull (indTarget);
}

void
CombatEntity::BindFields (Database::Statement& stmt, const unsigned indHp,
                          const unsigned indCanRegen) const
{
  bool canRegen = oldCanRegen;
  if (hp.IsDirty () || regenData.IsDirty ())
    canRegen = ComputeCanRegen (hp.Get (), regenData.Get ());

  stmt.BindProto (indHp, hp);
  stmt.Bind (indCanRegen, canRegen);
}

void
CombatEntity::Validate () const
{
#ifdef ENABLE_SLOW_ASSERTS

  if (!isNew && !IsDirtyCombatData ())
    CHECK_EQ (attackRange, FindAttackRange (pb.combat_data ()));

#endif // ENABLE_SLOW_ASSERTS
}

HexCoord::IntT
CombatEntity::GetAttackRange () const
{
  CHECK (!isNew);
  CHECK (!IsDirtyCombatData ());
  return oldAttackRange;
}

bool
CombatEntity::ComputeCanRegen (const proto::HP& hp,
                               const proto::RegenData& regen)
{
  if (regen.shield_regeneration_mhp () == 0)
    return false;

  return hp.shield () < regen.max_hp ().shield ();
}

HexCoord::IntT
CombatEntity::FindAttackRange (const proto::CombatData& cd)
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
