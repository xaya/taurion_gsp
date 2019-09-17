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

#include <glog/logging.h>

namespace pxd
{

Character::Character (Database& d, const std::string& o, const Faction f)
  : db(d), id(db.GetNextId ()), owner(o), faction(f),
    pos(0, 0), busy(0),
    isNew(true), dirtyFields(true)
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
  pos = HexCoord (res.Get<CharacterResult::x> (),
                  res.Get<CharacterResult::y> ());
  volatileMv = res.GetProto<CharacterResult::volatilemv> ();
  hp = res.GetProto<CharacterResult::hp> ();
  regenData = res.GetProto<CharacterResult::regendata> ();
  busy = res.Get<CharacterResult::busy> ();
  data = res.GetProto<CharacterResult::proto> ();

  VLOG (1) << "Fetched character with ID " << id << " from database result";
  Validate ();
}

Character::~Character ()
{
  Validate ();

  if (isNew || regenData.IsDirty () || data.IsDirty ())
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
           `ismoving`, `hastarget`, `regendata`, `proto`)
          VALUES
          (?1,
           ?2, ?3, ?4,
           ?5, ?6,
           ?7,
           ?101,
           ?102, ?103, ?104, ?105)
      )");

      BindFieldValues (stmt);
      BindFactionParameter (stmt, 101, faction);
      stmt.Bind (102, data.Get ().has_movement ());
      stmt.Bind (103, data.Get ().has_target ());
      stmt.BindProto (104, regenData);
      stmt.BindProto (105, data);
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
              `busy` = ?7
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
  CHECK_NE (id, Database::EMPTY_ID);
  CHECK_GE (busy, 0);

  /* Since this method is always called when loading a character, we should
     not access any of the protocol buffer fields.  Otherwise we would
     counteract their lazyness, since we would always parse them anyway.
     That is not worth it for some extra "unneeded" checks.  */
}

void
Character::BindFieldValues (Database::Statement& stmt) const
{
  stmt.Bind (1, id);
  stmt.Bind (2, owner);
  stmt.Bind (3, pos.GetX ());
  stmt.Bind (4, pos.GetY ());
  stmt.BindProto (5, volatileMv);
  stmt.BindProto (6, hp);
  stmt.Bind (7, busy);
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

} // namespace pxd
