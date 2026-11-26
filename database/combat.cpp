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

#include <google/protobuf/util/message_differencer.h>

#include <glog/logging.h>

using google::protobuf::util::MessageDifferencer;

namespace pxd
{

constexpr HexCoord::IntT CombatEntity::NO_ATTACKS;

CombatEntity::CombatEntity (Database& d)
  : db(d), isNew(true),
    friendlyTargets(false), oldCanRegen(false),
    isDirty(true)
{
  hp.SetToDefault ();
  regenData.SetToDefault ();
  target.SetToDefault ();
  otherTargets.SetToDefault ();
}

void
CombatEntity::SetFullFields (Database::RowWriter& row) const
{
  row.SetProto ("regendata", regenData);

  const auto attackRange = FindAttackRange (GetCombatData (), false);
  if (attackRange == NO_ATTACKS)
    row.SetNull ("attackrange");
  else
    row.Set ("attackrange", attackRange);

  const auto friendlyRange = FindAttackRange (GetCombatData (), true);
  if (friendlyRange == NO_ATTACKS)
    row.SetNull ("friendlyrange");
  else
    row.Set ("friendlyrange", friendlyRange);
}

void
CombatEntity::SetFields (Database::RowWriter& row, const bool all) const
{
  const bool canRegen = GetNewCanRegen ();

  if (all || hp.IsDirty ())
    row.SetProto ("hp", hp);
  if (all || isDirty)
    row.Set ("friendlytargets", friendlyTargets);
  if (all || canRegen != oldCanRegen)
    row.Set ("canregen", canRegen);

  if (all || target.IsDirty ())
    {
      if (HasTarget ())
        row.SetProto ("target", target);
      else
        row.SetNull ("target");
    }

  if (all || otherTargets.IsDirty ())
    {
      if (otherTargets.Get ().entries_size () > 0)
        row.SetProto ("othertargets", otherTargets);
      else
        row.SetNull ("othertargets");
    }
}

bool
CombatEntity::GetNewCanRegen () const
{
  if (hp.IsDirty () || regenData.IsDirty ())
    return ComputeCanRegen (hp.Get (), regenData.Get ());
  return oldCanRegen;
}

void
CombatEntity::Validate () const
{
#ifdef ENABLE_SLOW_ASSERTS

  if (!isNew && !IsDirtyCombatData ())
    {
      const auto& cd = GetCombatData ();
      CHECK_EQ (oldAttackRange, FindAttackRange (cd, false));
      CHECK_EQ (oldFriendlyRange, FindAttackRange (cd, true));
    }

  if (!regenData.IsDirty () && !hp.IsDirty ())
    CHECK_EQ (oldCanRegen, ComputeCanRegen (hp.Get (), regenData.Get ()));

  if (!HasTarget ())
    CHECK_EQ (otherTargets.Get ().entries_size (), 0)
        << "Other targets without a target";

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
  SetOtherTargets (proto::OtherTargets ());
}

void
CombatEntity::SetTarget (const proto::TargetId& t)
{
  /* Keeping the same target does not need a database update.  */
  if (!HasTarget () || target.Get ().type () != t.type ()
        || target.Get ().id () != t.id ())
    target.Mutable () = t;
  CHECK (HasTarget ());
}

void
CombatEntity::SetOtherTargets (const proto::OtherTargets& t)
{
  CHECK (HasTarget () || t.entries_size () == 0)
      << "Setting other targets without a target";

  /* As with the target, they are only written when they change.  */
  if (!MessageDifferencer::Equals (otherTargets.Get (), t))
    otherTargets.Mutable () = t;
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
