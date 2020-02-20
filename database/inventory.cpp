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

#include "inventory.hpp"

#include <glog/logging.h>

namespace pxd
{

/* Make sure that the product of item quantity and dual value (according to
   the respective limits) can be computed safely.  */
static_assert ((MAX_ITEM_QUANTITY * MAX_ITEM_DUAL) / MAX_ITEM_DUAL
                  == MAX_ITEM_QUANTITY,
               "item quantity and dual limits overflow multiplication");

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

Inventory::QuantityT
Inventory::GetFungibleCount (const std::string& type) const
{
  const auto& fungible = data.Get ().fungible ();
  const auto mit = fungible.find (type);
  if (mit == fungible.end ())
    return 0;
  return mit->second;
}

void
Inventory::SetFungibleCount (const std::string& type, const QuantityT count)
{
  CHECK_GE (count, 0);
  CHECK_LE (count, MAX_ITEM_QUANTITY);

  auto& fungible = *data.Mutable ().mutable_fungible ();

  if (count == 0)
    fungible.erase (type);
  else
    fungible[type] = count;
}

void
Inventory::AddFungibleCount (const std::string& type, const QuantityT count)
{
  CHECK_GE (count, -MAX_ITEM_QUANTITY);
  CHECK_LE (count, MAX_ITEM_QUANTITY);

  /* Instead of getting and then setting the value using the existing methods,
     we could just query the map once and update directly.  But doing so would
     require us to duplicate some of the logic (or refactor the code), so it
     seems not worth it unless this becomes an actual bottleneck.  */

  const auto previous = GetFungibleCount (type);
  SetFungibleCount (type, previous + count);
}

int64_t
Inventory::Product (const QuantityT amount, const int64_t dual)
{
  CHECK_GE (amount, -MAX_ITEM_QUANTITY);
  CHECK_LE (amount, MAX_ITEM_QUANTITY);

  CHECK_GE (dual, -MAX_ITEM_DUAL);
  CHECK_LE (dual, MAX_ITEM_DUAL);

  return amount * dual;
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

BuildingInventory::BuildingInventory (Database& d, const Database::IdT b,
                                      const std::string& a)
  : db(d), building(b), account(a)
{
  VLOG (1)
      << "Constructed an empty building inventory for building " << building
      << " and account " << account;
}

BuildingInventory::BuildingInventory (
    Database& d, const Database::Result<BuildingInventoryResult>& res)
  : db(d)
{
  building = res.Get<BuildingInventoryResult::building> ();
  account = res.Get<BuildingInventoryResult::account> ();
  inventory = res.GetProto<BuildingInventoryResult::inventory> ();
  VLOG (1)
      << "Created building inventory for building " << building
      << " and account " << account << " from database";
}

BuildingInventory::~BuildingInventory ()
{
  if (!inventory.IsDirty ())
    {
      VLOG (1)
          << "Building inventory for " << building << " and "
          << account << " is not dirty";
      return;
    }

  if (inventory.IsEmpty ())
    {
      VLOG (1)
          << "Building inventory for " << building
          << " and " << account << " is now empty, updating DB";

      auto stmt = db.Prepare (R"(
        DELETE FROM `building_inventories`
          WHERE `building` = ?1 AND `account` = ?2
      )");

      stmt.Bind (1, building);
      stmt.Bind (2, account);
      stmt.Execute ();
      return;
    }

  VLOG (1)
      << "Updating non-empty building inventory for " << building
      << " and " << account;

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `building_inventories`
      (`building`, `account`, `inventory`)
      VALUES (?1, ?2, ?3)
  )");

  stmt.Bind (1, building);
  stmt.Bind (2, account);
  stmt.BindProto (3, inventory.GetProtoForBinding ());
  stmt.Execute ();
}

BuildingInventoriesTable::Handle
BuildingInventoriesTable::GetFromResult (
    const Database::Result<BuildingInventoryResult>& res)
{
  return Handle (new BuildingInventory (db, res));
}

BuildingInventoriesTable::Handle
BuildingInventoriesTable::Get (const Database::IdT b, const std::string& a)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `building_inventories`
      WHERE `building` = ?1 AND `account` = ?2
  )");
  stmt.Bind (1, b);
  stmt.Bind (2, a);
  auto res = stmt.Query<BuildingInventoryResult> ();

  if (!res.Step ())
    return Handle (new BuildingInventory (db, b, a));

  auto r = GetFromResult (res);
  CHECK (!res.Step ());
  return r;
}

Database::Result<BuildingInventoryResult>
BuildingInventoriesTable::QueryAll ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `building_inventories`
      ORDER BY `building`, `account`
  )");
  return stmt.Query<BuildingInventoryResult> ();
}

Database::Result<BuildingInventoryResult>
BuildingInventoriesTable::QueryForBuilding (const Database::IdT building)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `building_inventories`
      WHERE `building` = ?1
      ORDER BY `account`
  )");
  stmt.Bind (1, building);

  return stmt.Query<BuildingInventoryResult> ();
}

void
BuildingInventoriesTable::RemoveBuilding (const Database::IdT building)
{
  auto stmt = db.Prepare (R"(
    DELETE FROM `building_inventories`
      WHERE `building` = ?1
  )");
  stmt.Bind (1, building);
  stmt.Execute ();
}

/* ************************************************************************** */

} // namespace pxd
