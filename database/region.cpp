/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

Region::Region (Database& d, const unsigned h, const RegionMap::IdT i)
  : db(d), currentHeight(h), id(i), resourceLeft(0), dirtyFields(false)
{
  VLOG (1) << "Created instance for empty region with ID " << id;
  data.SetToDefault ();
}

Region::Region (Database& d, const unsigned h,
                const Database::Result<RegionResult>& res)
  : db(d), currentHeight(h), dirtyFields(false)
{
  id = res.Get<RegionResult::id> ();
  resourceLeft = res.Get<RegionResult::resourceleft> ();
  data = res.GetProto<RegionResult::proto> ();

  if (currentHeight != RegionsTable::HEIGHT_READONLY)
    {
      const auto modified = res.Get<RegionResult::modifiedheight> ();
      LOG_IF (WARNING, currentHeight < modified)
          << "Region " << id << " is has current block height " << currentHeight
          << " set, but was last modified at height " << modified
          << "!  This is probably fine in unit tests";
    }

  VLOG (1) << "Created region data for ID " << id << " from database result";
}

Region::~Region ()
{
  if (data.IsDirty ())
    {
      VLOG (1) << "Updating dirty region " << id << " including proto data";
      CHECK_NE (currentHeight, RegionsTable::HEIGHT_READONLY)
          << "Region table is readonly";

      auto stmt = db.Prepare (R"(
        INSERT OR REPLACE INTO `regions`
          (`id`, `modifiedheight`, `resourceleft`, `proto`)
          VALUES (?1, ?2, ?3, ?4)
      )");

      stmt.Bind (1, id);
      stmt.Bind (2, currentHeight);
      stmt.Bind (3, resourceLeft);
      stmt.BindProto (4, data);
      stmt.Execute ();

      return;
    }

  if (dirtyFields)
    {
      VLOG (1) << "Updating dirty region " << id << " only in non-proto fields";
      CHECK_NE (currentHeight, RegionsTable::HEIGHT_READONLY)
          << "Region table is readonly";

      auto stmt = db.Prepare (R"(
        UPDATE `regions`
          SET `modifiedheight` = ?2, `resourceleft` = ?3
          WHERE `id` = ?1
      )");

      stmt.Bind (1, id);
      stmt.Bind (2, currentHeight);
      stmt.Bind (3, resourceLeft);
      stmt.Execute ();

      return;
    }

  VLOG (1) << "Region " << id << " is not dirty, no update";
}

Inventory::QuantityT
Region::GetResourceLeft () const
{
  CHECK (GetProto ().has_prospection ())
      << "Region " << id << " has not been prospected yet";
  return resourceLeft;
}

void
Region::SetResourceLeft (const Inventory::QuantityT value)
{
  CHECK (GetProto ().has_prospection ())
      << "Region " << id << " has not been prospected yet";
  resourceLeft = value;
  dirtyFields = true;
}

RegionsTable::Handle
RegionsTable::GetFromResult (const Database::Result<RegionResult>& res)
{
  return Handle (new Region (db, height, res));
}

RegionsTable::Handle
RegionsTable::GetById (const RegionMap::IdT id)
{
  auto stmt = db.Prepare ("SELECT * FROM `regions` WHERE `id` = ?1");
  stmt.Bind (1, id);
  auto res = stmt.Query<RegionResult> ();

  if (!res.Step ())
    return Handle (new Region (db, height, id));

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

Database::Result<RegionResult>
RegionsTable::QueryModifiedSince (const unsigned h)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `regions`
      WHERE `modifiedheight` >= ?1
      ORDER BY `id`
  )");
  stmt.Bind (1, h);

  return stmt.Query<RegionResult> ();
}

} // namespace pxd
