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

#include "region.hpp"

namespace pxd
{

Region::Region (Database& d, const RegionMap::IdT i)
  : db(d), id(i), dirty(false)
{
  VLOG (1) << "Created instance for empty region with ID " << id;
}

Region::Region (Database& d, const Database::Result<RegionResult>& res)
  : db(d), dirty(false)
{
  id = res.Get<RegionResult::id> ();
  res.GetProto<RegionResult::proto> (data);

  VLOG (1) << "Created region data for ID " << id << " from database result";
}

Region::~Region ()
{
  if (!dirty)
    {
      VLOG (1) << "Region " << id << " is not dirty, no update";
      return;
    }

  VLOG (1) << "Updating dirty region " << id << " in the database";
  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `regions`
      (`id`, `proto`)
      VALUES (?1, ?2)
  )");

  stmt.Bind (1, id);
  stmt.BindProto (2, data);
  stmt.Execute ();
}

RegionsTable::Handle
RegionsTable::GetFromResult (const Database::Result<RegionResult>& res)
{
  return Handle (new Region (db, res));
}

RegionsTable::Handle
RegionsTable::GetById (const RegionMap::IdT id)
{
  auto stmt = db.Prepare ("SELECT * FROM `regions` WHERE `id` = ?1");
  stmt.Bind (1, id);
  auto res = stmt.Query<RegionResult> ();

  if (!res.Step ())
    return Handle (new Region (db, id));

  auto r = GetFromResult (res);
  CHECK (!res.Step ());
  return r;
}

Database::Result<RegionResult>
RegionsTable::QueryNonTrivial ()
{
  auto stmt = db.Prepare ("SELECT * FROM `regions` ORDER BY `id`");
  return stmt.Query<RegionResult> ();
}

} // namespace pxd
