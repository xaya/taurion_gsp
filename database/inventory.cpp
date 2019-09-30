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

#include "inventory.hpp"

#include <glog/logging.h>

namespace pxd
{

/* ************************************************************************** */

Inventory::Inventory ()
{
  data.SetToDefault ();
}

Inventory::Inventory (LazyProto<proto::Inventory>&& d)
{
  *this = std::move (d);
}

Inventory&
Inventory::operator= (LazyProto<proto::Inventory>&& d)
{
  data = std::move (d);
  return *this;
}

bool
Inventory::IsEmpty () const
{
  return data.Get ().fungible ().empty ();
}

uint64_t
Inventory::GetFungibleCount (const std::string& type) const
{
  const auto& fungible = data.Get ().fungible ();
  const auto mit = fungible.find (type);
  if (mit == fungible.end ())
    return 0;
  return mit->second;
}

void
Inventory::SetFungibleCount (const std::string& type, const uint64_t count)
{
  auto& fungible = *data.Mutable ().mutable_fungible ();

  if (count == 0)
    fungible.erase (type); 
  else
    fungible[type] = count;
}

/* ************************************************************************** */

GroundLoot::GroundLoot (Database& d, const HexCoord& pos)
  : db(d), coord(pos)
{
  VLOG (1) << "Constructed an empty ground-loot instance for " << coord;
}

GroundLoot::GroundLoot (Database& d,
                        const Database::Result<GroundLootResult>& res)
  : db(d)
{
  coord = GetCoordFromColumn (res);
  inventory = res.GetProto<GroundLootResult::inventory> ();
  VLOG (1) << "Created ground-loot instance for " << coord << " from database";
}

GroundLoot::~GroundLoot ()
{
  if (!inventory.IsDirty ())
    {
      VLOG (1) << "Ground loot at " << coord << " is not dirty";
      return;
    }

  if (inventory.IsEmpty ())
    {
      VLOG (1) << "Ground loot at " << coord << " is now empty, updating DB";

      auto stmt = db.Prepare (R"(
        DELETE FROM `ground_loot`
          WHERE `x` = ?1 AND `y` = ?2
      )");

      BindCoordParameter (stmt, 1, 2, coord);
      stmt.Execute ();
      return;
    }

  VLOG (1) << "Updating non-empty ground loot at " << coord;

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `ground_loot`
      (`x`, `y`, `inventory`)
      VALUES (?1, ?2, ?3)
  )");

  BindCoordParameter (stmt, 1, 2, coord);
  stmt.BindProto (3, inventory.GetProtoForBinding ());
  stmt.Execute ();
}

/* ************************************************************************** */

GroundLootTable::Handle
GroundLootTable::GetFromResult (const Database::Result<GroundLootResult>& res)
{
  return Handle (new GroundLoot (db, res));
}

GroundLootTable::Handle
GroundLootTable::GetByCoord (const HexCoord& coord)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `ground_loot`
      WHERE `x` = ?1 AND `y` = ?2
  )");
  BindCoordParameter (stmt, 1, 2, coord);
  auto res = stmt.Query<GroundLootResult> ();

  if (!res.Step ())
    return Handle (new GroundLoot (db, coord));

  auto r = GetFromResult (res);
  CHECK (!res.Step ());
  return r;
}

Database::Result<GroundLootResult>
GroundLootTable::QueryNonEmpty ()
{
  auto stmt = db.Prepare ("SELECT * FROM `ground_loot` ORDER BY `x`, `y`");
  return stmt.Query<GroundLootResult> ();
}

/* ************************************************************************** */

} // namespace pxd
