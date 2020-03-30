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

#include "ongoing.hpp"

namespace pxd
{

OngoingOperation::OngoingOperation (Database& d)
  : db(d), id(db.GetNextId ()), height(0),
    characterId(Database::EMPTY_ID), buildingId(Database::EMPTY_ID),
    dirtyFields(true)
{
  VLOG (1) << "Created new ongoing operation with ID " << id;
  data.SetToDefault ();
}

OngoingOperation::OngoingOperation (Database& d,
                                    const Database::Result<OngoingResult>& res)
  : db(d), dirtyFields(false)
{
  id = res.Get<OngoingResult::id> ();
  height = res.Get<OngoingResult::height> ();
  characterId = res.Get<OngoingResult::character> ();
  buildingId = res.Get<OngoingResult::building> ();
  data = res.GetProto<OngoingResult::proto> ();

  VLOG (1) << "Created ongoing instance for ID " << id << " from database";
}

OngoingOperation::~OngoingOperation ()
{
  if (!dirtyFields && !data.IsDirty ())
    {
      VLOG (1) << "Ongoing " << id << " is not dirty";
      return;
    }

  VLOG (1) << "Updating dirty ongoing " << id << " in the database";

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `ongoing_operations`
      (`id`, `height`, `character`, `building`, `proto`)
      VALUES (?1, ?2, ?3, ?4, ?5)
  )");

  stmt.Bind (1, id);
  stmt.Bind (2, height);

  if (characterId == Database::EMPTY_ID)
    stmt.BindNull (3);
  else
    stmt.Bind (3, characterId);

  if (buildingId == Database::EMPTY_ID)
    stmt.BindNull (4);
  else
    stmt.Bind (4, buildingId);

  stmt.BindProto (5, data);

  stmt.Execute ();
}

OngoingsTable::Handle
OngoingsTable::CreateNew ()
{
  return Handle (new OngoingOperation (db));
}

OngoingsTable::Handle
OngoingsTable::GetFromResult (const Database::Result<OngoingResult>& res)
{
  return Handle (new OngoingOperation (db, res));
}

OngoingsTable::Handle
OngoingsTable::GetById (const Database::IdT id)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `ongoing_operations`
      WHERE `id` = ?1
  )");
  stmt.Bind (1, id);
  auto res = stmt.Query<OngoingResult> ();
  if (!res.Step ())
    return nullptr;

  auto op = GetFromResult (res);
  CHECK (!res.Step ());
  return op;
}

Database::Result<OngoingResult>
OngoingsTable::QueryAll ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `ongoing_operations`
      ORDER BY `id`
  )");
  return stmt.Query<OngoingResult> ();
}

Database::Result<OngoingResult>
OngoingsTable::QueryForBuilding (const Database::IdT id)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `ongoing_operations`
      WHERE `building` = ?1
      ORDER BY `id`
  )");
  stmt.Bind (1, id);
  return stmt.Query<OngoingResult> ();
}

Database::Result<OngoingResult>
OngoingsTable::QueryForHeight (const unsigned h)
{
  /* There should never be any entries *less* than the current block height
     in the database.  We query for less-of-equal anyway, so that we can then
     assert this while processing them.  */
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `ongoing_operations`
      WHERE `height` <= ?1
      ORDER BY `id`
  )");
  stmt.Bind (1, h);
  return stmt.Query<OngoingResult> ();
}

void
OngoingsTable::DeleteForCharacter (const Database::IdT id)
{
  auto stmt = db.Prepare (R"(
    DELETE FROM `ongoing_operations`
      WHERE `character` = ?1
  )");
  stmt.Bind (1, id);
  stmt.Execute ();
}

void
OngoingsTable::DeleteForBuilding (const Database::IdT id)
{
  auto stmt = db.Prepare (R"(
    DELETE FROM `ongoing_operations`
      WHERE `building` = ?1
  )");
  stmt.Bind (1, id);
  stmt.Execute ();
}

void
OngoingsTable::DeleteForHeight (const unsigned h)
{
  /* We only remove by exact height (not less-or-equal) so that any rows
     with an invalid height (should not happen) will not be silently removed.
     They should instead come up when processing next and assert-fail.  */
  auto stmt = db.Prepare (R"(
    DELETE FROM `ongoing_operations`
      WHERE `height` = ?1
  )");
  stmt.Bind (1, h);
  stmt.Execute ();
}

} // namespace pxd
