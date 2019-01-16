#include "character.hpp"

#include <glog/logging.h>

namespace pxd
{

Character::Character (Database& d, const std::string& o, const std::string& n)
  : db(d), id(db.GetNextId ()), owner(o), name(n),
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
  pos = HexCoord (res.Get<int> ("x"), res.Get<int> ("y"));
  res.GetProto ("proto", data);

  VLOG (1) << "Fetched character with ID " << id << " from database result";
}

Character::~Character ()
{
  CHECK_GT (id, 0);
  CHECK_NE (name, "");

  if (dirtyProto)
    {
      VLOG (1)
          << "Character " << id
          << " has been modified including proto data, updating DB";
      auto stmt = db.Prepare (R"(
        INSERT OR REPLACE INTO `characters`
          (`id`, `owner`, `name`, `x`, `y`, `ismoving`, `proto`) VALUES
          (?1, ?2, ?3, ?4, ?5, ?6, ?7)
      )");

      stmt.Bind<int> (1, id);
      stmt.Bind (2, owner);
      stmt.Bind (3, name);
      stmt.Bind<int> (4, pos.GetX ());
      stmt.Bind<int> (5, pos.GetY ());
      stmt.Bind (6, data.has_movement ());
      stmt.BindProto (7, data);

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
          SET `owner` = ?2, `name` = ?3,
              `x` = ?4, `y` = ?5
          WHERE `id` = ?1
      )");

      stmt.Bind<int> (1, id);
      stmt.Bind (2, owner);
      stmt.Bind (3, name);
      stmt.Bind<int> (4, pos.GetX ());
      stmt.Bind<int> (5, pos.GetY ());

      stmt.Execute ();
      return;
    }

  VLOG (1) << "Character " << id << " is not dirty, no update";
}

CharacterTable::Handle
CharacterTable::CreateNew (const std::string& owner, const std::string&  name)
{
  return Handle (new Character (db, owner, name));
}

CharacterTable::Handle
CharacterTable::GetFromResult (const Database::Result& res)
{
  return Handle (new Character (db, res));
}

CharacterTable::Handle
CharacterTable::GetById (const unsigned id)
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
