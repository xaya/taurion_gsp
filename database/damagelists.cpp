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

#include "damagelists.hpp"

#include <glog/logging.h>

namespace pxd
{

void
DamageLists::RemoveOld (const unsigned n)
{
  CHECK_NE (height, NO_HEIGHT);

  VLOG (1)
    << "Removing damage-list entries with height " << n << " before " << height;

  if (n > height)
    return;

  auto stmt = db.Prepare (R"(
    DELETE FROM `damage_lists`
      WHERE `height` <= ?1
  )");
  stmt.Bind (1, height - n);

  stmt.Execute ();
}

void
DamageLists::AddEntry (const Database::IdT victim, const Database::IdT attacker)
{
  CHECK_NE (height, NO_HEIGHT);

  VLOG (1)
      << "Adding damage-list entry for height " << height << ": "
      << attacker << " damaged " << victim;

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `damage_lists`
      (`victim`, `attacker`, `height`)
      VALUES (?1, ?2, ?3)
  )");
  stmt.Bind (1, victim);
  stmt.Bind (2, attacker);
  stmt.Bind (3, height);

  stmt.Execute ();
}

void
DamageLists::RemoveCharacter (const Database::IdT id)
{
  VLOG (1) << "Removing character " << id << " from damage lists...";

  auto stmt = db.Prepare (R"(
    DELETE FROM `damage_lists`
      WHERE `victim` = ?1 OR `attacker` = ?1
  )");
  stmt.Bind (1, id);

  stmt.Execute ();
}

DamageLists::Attackers
DamageLists::GetAttackers (const Database::IdT victim) const
{
  auto stmt = db.Prepare (R"(
    SELECT `attacker` FROM `damage_lists`
      WHERE `victim` = ?1
      ORDER BY `attacker` ASC
  )");
  stmt.Bind (1, victim);

  class AttackerResult
  {};

  auto res = stmt.Query<AttackerResult> ();
  Attackers attackers;
  while (res.Step ())
    {
      const auto insert = attackers.insert (res.Get<int64_t> ("attacker"));
      CHECK (insert.second);
    }

  return attackers;
}

} // namespace pxd
