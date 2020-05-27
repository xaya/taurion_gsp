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

#include "character.hpp"

#include "proto/roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

Character::Character (Database& d, const std::string& o, const Faction f)
  : CombatEntity(d), id(db.GetNextId ()), owner(o), faction(f),
    pos(0, 0), inBuilding(Database::EMPTY_ID),
    enterBuilding(Database::EMPTY_ID),
    dirtyFields(true)
{
  VLOG (1)
      << "Created new character with ID " << id << ": "
      << "owner=" << owner;
  volatileMv.SetToDefault ();
  effects.SetToDefault ();
  data.SetToDefault ();
  Validate ();
}

Character::Character (Database& d, const Database::Result<CharacterResult>& res)
  : CombatEntity(d, res), dirtyFields(false)
{
  id = res.Get<CharacterResult::id> ();
  owner = res.Get<CharacterResult::owner> ();
  faction = GetFactionFromColumn (res);

  if (res.IsNull<CharacterResult::inbuilding> ())
    {
      inBuilding = Database::EMPTY_ID;
      pos = GetCoordFromColumn (res);
    }
  else
    inBuilding = res.Get<CharacterResult::inbuilding> ();

  if (res.IsNull<CharacterResult::enterbuilding> ())
    enterBuilding = Database::EMPTY_ID;
  else
    enterBuilding = res.Get<CharacterResult::enterbuilding> ();

  if (res.IsNull<CharacterResult::effects> ())
    effects.SetToDefault ();
  else
    effects = res.GetProto<CharacterResult::effects> ();

  volatileMv = res.GetProto<CharacterResult::volatilemv> ();
  inv = res.GetProto<CharacterResult::inventory> ();
  data = res.GetProto<CharacterResult::proto> ();

  VLOG (1) << "Fetched character with ID " << id << " from database result";
  Validate ();
}

Character::~Character ()
{
  Validate ();

  if (isNew || CombatEntity::IsDirtyFull ()
        || inv.IsDirty () || effects.IsDirty () || data.IsDirty ())
    {
      VLOG (1)
          << "Character " << id
          << " has been modified including proto data, updating DB";
      auto stmt = db.Prepare (R"(
        INSERT OR REPLACE INTO `characters`
          (`id`,
           `owner`, `x`, `y`,
           `inbuilding`, `enterbuilding`,
           `volatilemv`, `hp`,
           `canregen`,
           `faction`,
           `ismoving`, `ismining`, `attackrange`,
           `regendata`, `target`, `inventory`, `effects`, `proto`)
          VALUES
          (?1,
           ?2, ?3, ?4,
           ?5, ?6,
           ?7, ?8,
           ?9,
           ?101,
           ?102, ?103, ?104,
           ?105, ?106, ?107, ?108, ?109)
      )");

      BindFieldValues (stmt);
      CombatEntity::BindFullFields (stmt, 105, 106, 104);

      if (effects.IsEmpty ())
        stmt.BindNull (108);
      else
        stmt.BindProto (108, effects);

      BindFactionParameter (stmt, 101, faction);
      stmt.Bind (102, data.Get ().has_movement ());
      stmt.Bind (103, data.Get ().mining ().active ());
      stmt.BindProto (107, inv.GetProtoForBinding ());
      stmt.BindProto (109, data);
      stmt.Execute ();

      return;
    }

  if (dirtyFields || volatileMv.IsDirty () || CombatEntity::IsDirtyFields ())
    {
      VLOG (1)
          << "Character " << id << " has been modified in the DB fields only,"
          << " updating those";

      auto stmt = db.Prepare (R"(
        UPDATE `characters`
          SET `owner` = ?2,
              `x` = ?3, `y` = ?4,
              `inbuilding` = ?5,
              `enterbuilding` = ?6,
              `volatilemv` = ?7,
              `hp` = ?8,
              `canregen` = ?9
          WHERE `id` = ?1
      )");

      BindFieldValues (stmt);
      stmt.Execute ();
      return;
    }

  VLOG (1) << "Character " << id << " is not dirty, no update";
}

void
Character::Validate () const
{
  CombatEntity::Validate ();

  CHECK_NE (id, Database::EMPTY_ID);

  /* Since this method is always called when loading a character, we should
     not access any of the protocol buffer fields.  Otherwise we would
     counteract their lazyness, since we would always parse them anyway.
     Hence, all further checks are subject to "slow assertions".  */

#ifdef ENABLE_SLOW_ASSERTS

  const auto& pb = data.Get ();

  if (IsBusy ())
    CHECK (!pb.has_movement ()) << "Busy character should not be moving";

  if (!regenData.IsDirty () && !hp.IsDirty ())
    CHECK_EQ (oldCanRegen, ComputeCanRegen ());

  CHECK_LE (UsedCargoSpace (), pb.cargo_space ());

  CHECK (!pb.mining ().active () || !pb.has_movement ())
      << "Character " << id << " is moving and mining at the same time";

#endif // ENABLE_SLOW_ASSERTS
}

void
Character::BindFieldValues (Database::Statement& stmt) const
{
  CombatEntity::BindFields (stmt, 8, 9);

  stmt.Bind (1, id);
  stmt.Bind (2, owner);
  if (IsInBuilding ())
    {
      stmt.BindNull (3);
      stmt.BindNull (4);
      stmt.Bind (5, inBuilding);
    }
  else
    {
      BindCoordParameter (stmt, 3, 4, pos);
      stmt.BindNull (5);
    }
  if (enterBuilding == Database::EMPTY_ID)
    stmt.BindNull (6);
  else
    stmt.Bind (6, enterBuilding);
  stmt.BindProto (7, volatileMv);
}

const HexCoord&
Character::GetPosition () const
{
  CHECK (!IsInBuilding ());
  return pos;
}

Database::IdT
Character::GetBuildingId () const
{
  CHECK (IsInBuilding ());
  return inBuilding;
}

bool
Character::IsBusy () const
{
  const auto& pb = GetProto ();
  if (!pb.has_ongoing ())
    return false;

  CHECK_GT (pb.ongoing (), 0);
  return true;
}

uint64_t
Character::UsedCargoSpace () const
{
  uint64_t res = 0;
  for (const auto& entry : inv.GetFungible ())
    {
      const auto& ro = RoConfig ().Item (entry.first);
      res += Inventory::Product (entry.second, ro.space ());
    }

  return res;
}

proto::TargetId
Character::GetIdAsTarget () const
{
  proto::TargetId res;
  res.set_type (proto::TargetId::TYPE_CHARACTER);
  res.set_id (id);
  return res;
}

CharacterTable::Handle
CharacterTable::CreateNew (const std::string& owner, const Faction faction)
{
  return Handle (new Character (db, owner, faction));
}

CharacterTable::Handle
CharacterTable::GetFromResult (const Database::Result<CharacterResult>& res)
{
  return Handle (new Character (db, res));
}

CharacterTable::Handle
CharacterTable::GetById (const Database::IdT id)
{
  auto stmt = db.Prepare ("SELECT * FROM `characters` WHERE `id` = ?1");
  stmt.Bind (1, id);
  auto res = stmt.Query<CharacterResult> ();
  if (!res.Step ())
    return nullptr;

  auto c = GetFromResult (res);
  CHECK (!res.Step ());
  return c;
}

Database::Result<CharacterResult>
CharacterTable::QueryAll ()
{
  auto stmt = db.Prepare ("SELECT * FROM `characters` ORDER BY `id`");
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryForOwner (const std::string& owner)
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `owner` = ?1 ORDER BY `id`
  )");
  stmt.Bind (1, owner);
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryForBuilding (const Database::IdT building)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `characters`
      WHERE `inbuilding` = ?1
      ORDER BY `id`
  )");
  stmt.Bind (1, building);
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryMoving ()
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `ismoving` ORDER BY `id`
  )");
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryMining ()
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `ismining` ORDER BY `id`
  )");
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryWithAttacks ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `characters`
      WHERE `attackrange` IS NOT NULL AND `inBuilding` IS NULL
      ORDER BY `id`
  )");
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryForRegen ()
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `canregen` ORDER BY `id`
  )");
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryWithTarget ()
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `characters`
      WHERE `target` IS NOT NULL
      ORDER BY `id`
  )");
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryForEnterBuilding ()
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters`
      WHERE `enterbuilding` IS NOT NULL
      ORDER BY `id`
  )");
  return stmt.Query<CharacterResult> ();
}

namespace
{

struct PositionResult : public ResultWithFaction, public ResultWithCoord
{
  RESULT_COLUMN (int64_t, id, 1);
};

} // anonymous namespace

void
CharacterTable::ProcessAllPositions (const PositionFcn& cb)
{
  auto stmt = db.Prepare (R"(
    SELECT `id`, `x`, `y`, `faction`
      FROM `characters`
      WHERE `inbuilding` IS NULL
      ORDER BY `id`
  )");

  auto res = stmt.Query<PositionResult> ();
  while (res.Step ())
    {
      const Database::IdT id = res.Get<PositionResult::id> ();
      const HexCoord pos = GetCoordFromColumn (res);
      const Faction f = GetFactionFromColumn (res);
      cb (id, pos, f);
    }
}

void
CharacterTable::DeleteById (const Database::IdT id)
{
  VLOG (1) << "Deleting character with ID " << id;

  auto stmt = db.Prepare (R"(
    DELETE FROM `characters` WHERE `id` = ?1
  )");
  stmt.Bind (1, id);
  stmt.Execute ();
}

namespace
{

struct CountResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, cnt, 1);
};

} // anonymous namespace

unsigned
CharacterTable::CountForOwner (const std::string& owner)
{
  auto stmt = db.Prepare (R"(
    SELECT COUNT(*) AS `cnt`
      FROM `characters`
      WHERE `owner` = ?1
      ORDER BY `id`
  )");
  stmt.Bind (1, owner);

  auto res = stmt.Query<CountResult> ();
  CHECK (res.Step ());
  const unsigned count = res.Get<CountResult::cnt> ();
  CHECK (!res.Step ());

  return count;
}

void
CharacterTable::ClearAllEffects ()
{
  VLOG (1) << "Clearing all combat effects on characters";

  auto stmt = db.Prepare (R"(
    UPDATE `characters`
      SET `effects` = NULL
      WHERE `effects` IS NOT NULL
  )");
  stmt.Execute ();
}

} // namespace pxd
