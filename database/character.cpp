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

#include "character.hpp"

#include "fighter.hpp"

#include "proto/roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

Character::Character (Database& d, const std::string& o, const Faction f)
  : db(d), id(db.GetNextId ()), owner(o), faction(f),
    pos(0, 0), busy(0),
    oldCanRegen(false), isNew(true), dirtyFields(true)
{
  VLOG (1)
      << "Created new character with ID " << id << ": "
      << "owner=" << owner;
  volatileMv.SetToDefault ();
  hp.SetToDefault ();
  regenData.SetToDefault ();
  data.SetToDefault ();
  Validate ();
}

Character::Character (Database& d, const Database::Result<CharacterResult>& res)
  : db(d), isNew(false), dirtyFields(false)
{
  id = res.Get<CharacterResult::id> ();
  owner = res.Get<CharacterResult::owner> ();
  faction = GetFactionFromColumn (res);
  pos = GetCoordFromColumn (res);
  volatileMv = res.GetProto<CharacterResult::volatilemv> ();
  hp = res.GetProto<CharacterResult::hp> ();
  regenData = res.GetProto<CharacterResult::regendata> ();
  busy = res.Get<CharacterResult::busy> ();
  inv = res.GetProto<CharacterResult::inventory> ();
  data = res.GetProto<CharacterResult::proto> ();
  attackRange = res.Get<CharacterResult::attackrange> ();
  oldCanRegen = res.Get<CharacterResult::canregen> ();
  hasTarget = res.Get<CharacterResult::hastarget> ();

  VLOG (1) << "Fetched character with ID " << id << " from database result";
  Validate ();
}

Character::~Character ()
{
  Validate ();

  bool canRegen = oldCanRegen;
  if (hp.IsDirty () || regenData.IsDirty ())
    canRegen = ComputeCanRegen ();

  if (isNew || regenData.IsDirty () || inv.IsDirty () || data.IsDirty ())
    {
      VLOG (1)
          << "Character " << id
          << " has been modified including proto data, updating DB";
      auto stmt = db.Prepare (R"(
        INSERT OR REPLACE INTO `characters`
          (`id`,
           `owner`, `x`, `y`,
           `volatilemv`, `hp`,
           `busy`,
           `faction`,
           `ismoving`, `ismining`, `attackrange`, `canregen`, `hastarget`,
           `regendata`, `inventory`, `proto`)
          VALUES
          (?1,
           ?2, ?3, ?4,
           ?5, ?6,
           ?7,
           ?101,
           ?102, ?103, ?104, ?105, ?106,
           ?107, ?108, ?109)
      )");

      BindFieldValues (stmt);
      BindFactionParameter (stmt, 101, faction);
      stmt.Bind (102, data.Get ().has_movement ());
      stmt.Bind (103, data.Get ().mining ().active ());
      stmt.Bind (104, FindAttackRange (data.Get ().combat_data ()));
      stmt.Bind (105, canRegen);
      stmt.Bind (106, data.Get ().has_target ());
      stmt.BindProto (107, regenData);
      stmt.BindProto (108, inv.GetProtoForBinding ());
      stmt.BindProto (109, data);
      stmt.Execute ();

      return;
    }

  if (dirtyFields || volatileMv.IsDirty () || hp.IsDirty ())
    {
      VLOG (1)
          << "Character " << id << " has been modified in the DB fields only,"
          << " updating those";

      auto stmt = db.Prepare (R"(
        UPDATE `characters`
          SET `owner` = ?2,
              `x` = ?3, `y` = ?4,
              `volatilemv` = ?5,
              `hp` = ?6,
              `busy` = ?7,
              `canregen` = ?101
          WHERE `id` = ?1
      )");

      BindFieldValues (stmt);
      stmt.Bind (101, canRegen);
      stmt.Execute ();

      return;
    }

  VLOG (1) << "Character " << id << " is not dirty, no update";
}

void
Character::Validate () const
{
  CHECK_NE (id, Database::EMPTY_ID);
  CHECK_GE (busy, 0);

  /* Since this method is always called when loading a character, we should
     not access any of the protocol buffer fields.  Otherwise we would
     counteract their lazyness, since we would always parse them anyway.
     Hence, all further checks are subject to "slow assertions".  */

#ifdef ENABLE_SLOW_ASSERTS

  const auto& pb = data.Get ();

  if (busy == 0)
    CHECK_EQ (pb.busy_case (), proto::Character::BUSY_NOT_SET);
  else
    {
      CHECK_NE (pb.busy_case (), proto::Character::BUSY_NOT_SET);
      CHECK (!pb.has_movement ()) << "Busy character should not be moving";
    }

  if (!isNew && !data.IsDirty ())
    {
      CHECK_EQ (hasTarget, pb.has_target ());
      CHECK_EQ (attackRange, FindAttackRange (pb.combat_data ()));
    }

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
  stmt.Bind (1, id);
  stmt.Bind (2, owner);
  BindCoordParameter (stmt, 3, 4, pos);
  stmt.BindProto (5, volatileMv);
  stmt.BindProto (6, hp);
  stmt.Bind (7, busy);
}

bool
Character::ComputeCanRegen () const
{
  const auto& regenPb = regenData.Get ();

  if (regenPb.shield_regeneration_mhp () == 0)
    return false;

  return hp.Get ().shield () < regenPb.max_hp ().shield ();
}

HexCoord::IntT
Character::GetAttackRange () const
{
  CHECK (!isNew);
  CHECK (!data.IsDirty ());
  return attackRange;
}

bool
Character::HasTarget () const
{
  CHECK (!isNew);
  CHECK (!data.IsDirty ());
  return hasTarget;
}

uint64_t
Character::UsedCargoSpace () const
{
  const auto& itemData = RoConfigData ().fungible_items ();

  uint64_t res = 0;
  for (const auto& entry : inv.GetFungible ())
    {
      const auto mit = itemData.find (entry.first);
      CHECK (mit != itemData.end ())
          << "Unknown item in character inventory: " << entry.first;
      res += Inventory::Product (entry.second, mit->second.space ());
    }

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
      WHERE `attackrange` > 0
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
    SELECT * FROM `characters` WHERE `hastarget` ORDER BY `id`
  )");
  return stmt.Query<CharacterResult> ();
}

Database::Result<CharacterResult>
CharacterTable::QueryBusyDone ()
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `busy` = 1 ORDER BY `id`
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

void
CharacterTable::DecrementBusy ()
{
  VLOG (1) << "Decrementing busy counter for all characters...";

  auto stmt = db.Prepare (R"(
    SELECT COUNT(*) AS `cnt` FROM `characters` WHERE `busy` = 1
  )");
  auto res = stmt.Query<CountResult> ();
  CHECK (res.Step ());
  CHECK_EQ (res.Get<CountResult::cnt> (), 0)
      << "DecrementBusy called but there are characters with busy=1";
  CHECK (!res.Step ());

  stmt = db.Prepare (R"(
    UPDATE `characters`
      SET `busy` = `busy` - 1
      WHERE `busy` > 0
  )");
  stmt.Execute ();
}

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

} // namespace pxd
