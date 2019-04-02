#include "character.hpp"

#include <glog/logging.h>

namespace pxd
{

Character::Character (Database& d, const std::string& o, const Faction f)
  : db(d), id(db.GetNextId ()), owner(o), faction(f),
    pos(0, 0), busy(0),
    dirtyFields(true), dirtyProto(true)
{
  VLOG (1)
      << "Created new character with ID " << id << ": "
      << "owner=" << owner;
  Validate ();
}

Character::Character (Database& d, const Database::Result& res)
  : db(d), dirtyFields(false), dirtyProto(false)
{
  CHECK_EQ (res.GetName (), "characters");
  id = res.Get<int64_t> ("id");
  owner = res.Get<std::string> ("owner");
  faction = GetFactionFromColumn (res, "faction");
  pos = HexCoord (res.Get<int64_t> ("x"), res.Get<int64_t> ("y"));
  res.GetProto ("volatilemv", volatileMv);
  res.GetProto ("hp", hp);
  busy = res.Get<int64_t> ("busy");
  res.GetProto ("proto", data);

  VLOG (1) << "Fetched character with ID " << id << " from database result";
  Validate ();
}

Character::~Character ()
{
  Validate ();

  if (dirtyProto)
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
           `ismoving`, `hastarget`, `proto`)
          VALUES
          (?1,
           ?2, ?3, ?4,
           ?5, ?6,
           ?7,
           ?101,
           ?102, ?103, ?104)
      )");

      BindFieldValues (stmt);
      BindFactionParameter (stmt, 101, faction);
      stmt.Bind (102, data.has_movement ());
      stmt.Bind (103, data.has_target ());
      stmt.BindProto (104, data);
      stmt.Execute ();

      return;
    }

  if (dirtyFields)
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

  if (busy == 0)
    CHECK_EQ (data.busy_case (), proto::Character::BUSY_NOT_SET);
  else
    {
      CHECK_NE (data.busy_case (), proto::Character::BUSY_NOT_SET);
      CHECK (!data.has_movement ()) << "Busy character should not be moving";
    }
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
CharacterTable::GetFromResult (const Database::Result& res)
{
  return Handle (new Character (db, res));
}

CharacterTable::Handle
CharacterTable::GetById (const Database::IdT id)
{
  auto stmt = db.Prepare ("SELECT * FROM `characters` WHERE `id` = ?1");
  stmt.Bind (1, id);
  auto res = stmt.Query ("characters");
  if (!res.Step ())
    return nullptr;

  auto c = GetFromResult (res);
  CHECK (!res.Step ());
  return c;
}

Database::Result
CharacterTable::QueryAll ()
{
  auto stmt = db.Prepare ("SELECT * FROM `characters` ORDER BY `id`");
  return stmt.Query ("characters");
}

Database::Result
CharacterTable::QueryForOwner (const std::string& owner)
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `owner` = ?1 ORDER BY `id`
  )");
  stmt.Bind (1, owner);
  return stmt.Query ("characters");
}

Database::Result
CharacterTable::QueryMoving ()
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `ismoving` ORDER BY `id`
  )");
  return stmt.Query ("characters");
}

Database::Result
CharacterTable::QueryWithTarget ()
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `hastarget` ORDER BY `id`
  )");
  return stmt.Query ("characters");
}

Database::Result
CharacterTable::QueryBusyDone ()
{
  auto stmt = db.Prepare (R"(
    SELECT * FROM `characters` WHERE `busy` = 1 ORDER BY `id`
  )");
  return stmt.Query ("characters");
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

void
CharacterTable::DecrementBusy ()
{
  VLOG (1) << "Decrementing busy counter for all characters...";

  auto stmt = db.Prepare (R"(
    SELECT COUNT(*) AS `cnt` FROM `characters` WHERE `busy` = 1
  )");
  auto res = stmt.Query ();
  CHECK (res.Step ());
  CHECK_EQ (res.Get<int64_t> ("cnt"), 0)
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
