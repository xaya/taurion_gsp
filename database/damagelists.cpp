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

  auto res = stmt.Query ();
  Attackers attackers;
  while (res.Step ())
    {
      const auto insert = attackers.insert (res.Get<int64_t> ("attacker"));
      CHECK (insert.second);
    }

  return attackers;
}

} // namespace pxd
