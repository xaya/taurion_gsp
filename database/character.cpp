#include "character.hpp"

#include <glog/logging.h>

namespace pxd
{

Character::Character (Database& d, const std::string& o, const std::string& n,
                      const Faction f)
  : db(d), id(db.GetNextId ()), owner(o), name(n), faction(f),
    pos(0, 0), partialStep(0),
    dirtyFields(true), dirtyProto(true)
{
  VLOG (1)
      << "Created new character with ID " << id << ": "
      << "owner=" << owner << ", name=" << name;
}

Character::Character (Database& d, const Database::Result& res)
  : db(d), dirtyFields(false), dirtyProto(false)
{
  CHECK_EQ (res.GetName (), "characters");
  id = res.Get<int> ("id");
  owner = res.Get<std::string> ("owner");
  name = res.Get<std::string> ("name");
  faction = GetFactionFromColumn (res, "faction");
  pos = HexCoord (res.Get<int> ("x"), res.Get<int> ("y"));
  partialStep = res.Get<int> ("partialstep");
  res.GetProto ("proto", data);

  VLOG (1) << "Fetched character with ID " << id << " from database result";
}

Character::~Character ()
{
  CHECK_NE (id, Database::EMPTY_ID);
  CHECK_NE (name, "");

  if (dirtyProto)
    {
      VLOG (1)
          << "Character " << id
          << " has been modified including proto data, updating DB";
      auto stmt = db.Prepare (R"(
        INSERT OR REPLACE INTO `characters`
          (`id`,
           `owner`, `x`, `y`, `partialstep`,
           `name`, `faction`,
           `ismoving`, `proto`)
          VALUES
          (?1,
           ?2, ?3, ?4, ?5,
           ?6, ?7,
           ?8, ?9)
      )");

      BindFieldValues (stmt);
      stmt.Bind (6, name);
      BindFactionParameter (stmt, 7, faction);
      stmt.Bind (8, data.has_movement ());
      stmt.BindProto (9, data);
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
          SET `owner` = ?2, `x` = ?3, `y` = ?4, `partialstep` = ?5
          WHERE `id` = ?1
      )");

      BindFieldValues (stmt);
      stmt.Execute ();

      return;
    }

  VLOG (1) << "Character " << id << " is not dirty, no update";
}

void
Character::BindFieldValues (Database::Statement& stmt) const
{
  stmt.Bind<int> (1, id);
  stmt.Bind (2, owner);
  stmt.Bind<int> (3, pos.GetX ());
  stmt.Bind<int> (4, pos.GetY ());
  if (partialStep == 0)
    stmt.BindNull (5);
  else
    stmt.Bind<int> (5, partialStep);
}

CharacterTable::Handle
CharacterTable::CreateNew (const std::string& owner, const std::string&  name,
                           const Faction faction)
{
  return Handle (new Character (db, owner, name, faction));
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
  stmt.Bind<int> (1, id);
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

bool
CharacterTable::IsValidName (const std::string& name)
{
  if (name.empty ())
    return false;

  auto stmt = db.Prepare (R"(
    SELECT COUNT(*) AS `cnt` FROM `characters` WHERE `name` = ?1
  )");
  stmt.Bind (1, name);

  auto res = stmt.Query ();
  CHECK (res.Step ());
  const int cnt = res.Get<int> ("cnt");
  CHECK (!res.Step ());

  VLOG (1) << "Name " << name << " is used " << cnt << " times";
  return cnt == 0;
}

} // namespace pxd
