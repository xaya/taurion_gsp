#include "region.hpp"

namespace pxd
{

Region::Region (Database& d, const RegionMap::IdT i)
  : db(d), id(i), dirty(false)
{
  VLOG (1) << "Created instance for empty region with ID " << id;
}

Region::Region (Database& d, const Database::Result& res)
  : db(d), dirty(false)
{
  CHECK_EQ (res.GetName (), "regions");
  id = res.Get<int64_t> ("id");
  res.GetProto ("proto", data);

  VLOG (1) << "Created region data for ID " << id << " from database result";
}

Region::~Region ()
{
  if (!dirty)
    {
      VLOG (1) << "Region " << id << " is not dirty, no update";
      return;
    }

  VLOG (1) << "Updating dirty region " << id << " in the database";
  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `regions`
      (`id`, `proto`)
      VALUES (?1, ?2)
  )");

  stmt.Bind (1, id);
  stmt.BindProto (2, data);
  stmt.Execute ();
}

RegionsTable::Handle
RegionsTable::GetFromResult (const Database::Result& res)
{
  return Handle (new Region (db, res));
}

RegionsTable::Handle
RegionsTable::GetById (const RegionMap::IdT id)
{
  auto stmt = db.Prepare ("SELECT * FROM `regions` WHERE `id` = ?1");
  stmt.Bind (1, id);
  auto res = stmt.Query ("regions");

  if (!res.Step ())
    return Handle (new Region (db, id));

  auto r = GetFromResult (res);
  CHECK (!res.Step ());
  return r;
}

Database::Result
RegionsTable::QueryNonTrivial ()
{
  auto stmt = db.Prepare ("SELECT * FROM `regions` ORDER BY `id`");
  return stmt.Query ("regions");
}

} // namespace pxd
