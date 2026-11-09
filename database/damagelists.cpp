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

DamageLists::~DamageLists ()
{
  /* An existing entry only gets its height updated, which (unlike a
     REPLACE that deletes and re-inserts the row) leaves the other
     indices alone, and only once it is old enough:  an unchanged row
     costs no write and no undo data.  */
  for (const auto& entry : pendingByVictim)
    {
      auto stmt = db.Prepare (R"(
        INSERT INTO `damage_lists`
          (`victim`, `attacker`, `height`)
          VALUES (?1, ?2, ?3)
          ON CONFLICT (`victim`, `attacker`) DO UPDATE
            SET `height` = `excluded`.`height`
            WHERE `height` <= `excluded`.`height` - ?4
      )");
      stmt.Bind (1, entry.first);
      stmt.Bind (2, entry.second);
      stmt.Bind (3, height);
      stmt.Bind (4, refresh);

      stmt.Execute ();
    }
}

void
DamageLists::RemoveOld (const unsigned n)
{
  CHECK_NE (height, NO_HEIGHT);
  CHECK (pendingByVictim.empty ());

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

  pendingByVictim.emplace (victim, attacker);
  pendingByAttacker.emplace (attacker, victim);
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

  RemovePending (pendingByVictim, pendingByAttacker, id);
  RemovePending (pendingByAttacker, pendingByVictim, id);
}

void
DamageLists::RemovePending (
    std::set<std::pair<Database::IdT, Database::IdT>>& by,
    std::set<std::pair<Database::IdT, Database::IdT>>& other,
    const Database::IdT id)
{
  auto it = by.lower_bound (std::make_pair (id, Database::IdT (0)));
  while (it != by.end () && it->first == id)
    {
      other.erase (std::make_pair (it->second, it->first));
      it = by.erase (it);
    }
}

namespace
{

struct AttackerResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, attacker, 1);
};

} // anonymous namespace

DamageLists::Attackers
DamageLists::GetAttackers (const Database::IdT victim) const
{
  auto stmt = db.Prepare (R"(
    SELECT `attacker` FROM `damage_lists`
      WHERE `victim` = ?1
      ORDER BY `attacker` ASC
  )");
  stmt.Bind (1, victim);

  auto res = stmt.Query<AttackerResult> ();
  Attackers attackers;
  while (res.Step ())
    {
      const Database::IdT att = res.Get<AttackerResult::attacker> ();
      const auto insert = attackers.insert (att);
      CHECK (insert.second);
    }

  /* Pending entries may refresh ones in the database.  */
  auto it = pendingByVictim.lower_bound (
      std::make_pair (victim, Database::IdT (0)));
  for (; it != pendingByVictim.end () && it->first == victim; ++it)
    attackers.insert (it->second);

  return attackers;
}

} // namespace pxd
