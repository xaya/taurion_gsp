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

#include "building.hpp"

#include "proto/roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

Building::Building (Database& d, const std::string& t,
                    const std::string& o, const Faction f)
  : CombatEntity(d), id(db.GetNextId ()), type(t), owner(o), faction(f),
    pos(0, 0), dirtyFields(true)
{
  VLOG (1)
      << "Created new building with ID " << id << ": "
      << "type=" << type << ", owner=" << owner;
  data.SetToDefault ();

  if (f == Faction::ANCIENT)
    CHECK_EQ (owner, "");
}

Building::Building (Database& d, const Database::Result<BuildingResult>& res)
  : CombatEntity(d, res), dirtyFields(false)
{
  id = res.Get<BuildingResult::id> ();
  type = res.Get<BuildingResult::type> ();
  faction = GetFactionFromColumn (res);
  if (faction != Faction::ANCIENT)
    owner = res.Get<BuildingResult::owner> ();
  pos = GetCoordFromColumn (res);
  data = res.GetProto<BuildingResult::proto> ();

  VLOG (1) << "Fetched building with ID " << id << " from database result";
}

Building::~Building ()
{
  /* For now, we only implement "full update".  For buildings, fields are
     not modified that often, so it seems not like a useful optimisation
     to specifically handle that case.  */

  if (isNew || CombatEntity::IsDirtyFull () || CombatEntity::IsDirtyFields ()
        || dirtyFields || data.IsDirty ())
    {
      VLOG (1)
          << "Building " << id << " has been modified, updating DB";

      auto stmt = db.Prepare (R"(
        INSERT OR REPLACE INTO `buildings`
          (`id`, `type`,
           `faction`, `owner`, `x`, `y`,
           `hp`, `regendata`, `target`,
           `attackrange`, `canregen`,
           `proto`)
          VALUES
          (?1, ?2,
           ?3, ?4, ?5, ?6,
           ?7, ?8, ?9,
           ?10, ?11,
           ?12)
      )");

      stmt.Bind (1, id);
      stmt.Bind (2, type);
      BindFactionParameter (stmt, 3, faction);
      if (faction == Faction::ANCIENT)
        stmt.BindNull (4);
      else
        stmt.Bind (4, owner);
      BindCoordParameter (stmt, 5, 6, pos);
      CombatEntity::BindFields (stmt, 7, 11);
      CombatEntity::BindFullFields (stmt, 8, 9, 10);
      stmt.BindProto (12, data);
      stmt.Execute ();

      return;
    }

  VLOG (1) << "Building " << id << " is not dirty, no update";
}

const std::string&
Building::GetOwner () const
{
  CHECK (faction != Faction::ANCIENT) << "Ancient building has no owner";
  return owner;
}

void
Building::SetOwner (const std::string& o)
{
  CHECK (faction != Faction::ANCIENT) << "Ancient building has no owner";
  dirtyFields = true;
  owner = o;
}

void
Building::SetCentre (const HexCoord& c)
{
  CHECK (isNew) << "Only new building can have its centre set";
  pos = c;
}

proto::TargetId
Building::GetIdAsTarget () const
{
  proto::TargetId res;
  res.set_type (proto::TargetId::TYPE_BUILDING);
  res.set_id (id);
  return res;
}

const proto::CombatEffects&
Building::GetEffects () const
{
  /* Buildings do not support effects, so we just return a
     default proto.  */
  return proto::CombatEffects::default_instance ();
}

proto::CombatEffects&
Building::MutableEffects ()
{
  /* Buildings do not support effects, so we just return a mutable
     dummy proto that can be freely modified (without affecting the
     outcome of anything else).

     We use a thread-local variable to avoid situations where two threads
     try to access and modify the same dummy proto at the same time in
     case we multi-thread some game logic later on.  */
  thread_local proto::CombatEffects dummy;
  return dummy;
}

const proto::BuildingData&
Building::RoConfigData () const
{
  const auto& buildings = RoConfig ()->building_types ();
  const auto mit = buildings.find (GetType ());
  CHECK (mit != buildings.end ())
      << "Building " << GetId () << " has undefined type: " << GetType ();
  return mit->second;
}

BuildingsTable::Handle
BuildingsTable::CreateNew (const std::string& type,
                           const std::string& owner, const Faction faction)
{
  return Handle (new Building (db, type, owner, faction));
}

BuildingsTable::Handle
BuildingsTable::GetFromResult (const Database::Result<BuildingResult>& res)
{
  return Handle (new Building (db, res));
}

BuildingsTable::Handle
BuildingsTable::GetById (const Database::IdT id)
{
  auto stmt = db.Prepare ("SELECT * FROM `buildings` WHERE `id` = ?1");
  stmt.Bind (1, id);
  auto res = stmt.Query<BuildingResult> ();
  if (!res.Step ())
    return nullptr;

  auto c = GetFromResult (res);
  CHECK (!res.Step ());
  return c;
}

Database::Result<BuildingResult>
BuildingsTable::QueryAll ()
{
  auto stmt = db.Prepare ("SELECT * FROM `buildings` ORDER BY `id`");
  return stmt.Query<BuildingResult> ();
}

void
BuildingsTable::DeleteById (const Database::IdT id)
{
  VLOG (1) << "Deleting building with ID " << id;

  auto stmt = db.Prepare (R"(
    DELETE FROM `buildings`
      WHERE `id` = ?1
  )");
  stmt.Bind (1, id);
  stmt.Execute ();
}

Database::Result<BuildingResult>
BuildingsTable::QueryWithAttacks ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `buildings`
      WHERE `attackrange` IS NOT NULL
      ORDER BY `id`
  )");
  return stmt.Query<BuildingResult> ();
}

Database::Result<BuildingResult>
BuildingsTable::QueryForRegen ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `buildings`
      WHERE `canregen`
      ORDER BY `id`
  )");
  return stmt.Query<BuildingResult> ();
}

Database::Result<BuildingResult>
BuildingsTable::QueryWithTarget ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `buildings`
      WHERE `target` IS NOT NULL
      ORDER BY `id`
  )");
  return stmt.Query<BuildingResult> ();
}

} // namespace pxd
