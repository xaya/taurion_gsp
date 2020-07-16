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

constexpr HexCoord::IntT CombatEntity::NO_ATTACKS;

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
                              const unsigned indAttackRange,
                              const unsigned indFriendlyRange) const
{
  stmt.BindProto (indRegenData, regenData);

  const auto attackRange = FindAttackRange (GetCombatData (), false);
  if (attackRange == NO_ATTACKS)
    stmt.BindNull (indAttackRange);
  else
    stmt.Bind (indAttackRange, attackRange);

  const auto friendlyRange = FindAttackRange (GetCombatData (), true);
  if (friendlyRange == NO_ATTACKS)
    stmt.BindNull (indFriendlyRange);
  else
    stmt.Bind (indFriendlyRange, friendlyRange);

  if (HasTarget ())
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
    {
      CHECK_EQ (oldAttackRange, FindAttackRange (pb.combat_data (), false));
      CHECK_EQ (oldFriendlyRange, FindAttackRange (pb.combat_data (), true));
    }

#endif // ENABLE_SLOW_ASSERTS
}

const proto::TargetId&
CombatEntity::GetTarget () const
{
  CHECK (HasTarget ());
  return target.Get ();
}

void
CombatEntity::ClearTarget ()
{
  if (HasTarget ())
    target.Mutable ().Clear ();
}

void
CombatEntity::SetTarget (const proto::TargetId& t)
{
  target.Mutable () = t;
  CHECK (HasTarget ());
}

HexCoord::IntT
CombatEntity::GetAttackRange (const bool friendly) const
{
  CHECK (!isNew);
  CHECK (!IsDirtyCombatData ());

  return friendly ? oldFriendlyRange : oldAttackRange;
}

bool
CombatEntity::ComputeCanRegen (const proto::HP& hp,
                               const proto::RegenData& regen)
{
  if (regen.regeneration_mhp ().shield () > 0
        && hp.shield () < regen.max_hp ().shield ())
    return true;

  if (regen.regeneration_mhp ().armour () > 0
        && hp.armour () < regen.max_hp ().armour ())
    return true;

  return false;
}

HexCoord::IntT
CombatEntity::FindAttackRange (const proto::CombatData& cd, const bool friendly)
{
  HexCoord::IntT res = NO_ATTACKS;
  for (const auto& attack : cd.attacks ())
    {
      if (attack.friendlies () != friendly)
        continue;

      HexCoord::IntT cur;
      if (attack.has_range ())
        cur = attack.range ();
      else
        {
          CHECK (attack.has_area ());
          cur = attack.area ();
        }

      if (res == NO_ATTACKS || cur > res)
        res = cur;
    }

  return res;
}

} // namespace pxd
