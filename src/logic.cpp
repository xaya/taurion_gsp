#include "logic.hpp"

#include "gamestatejson.hpp"
#include "moveprocessor.hpp"
#include "params.hpp"

#include "database/database.hpp"
#include "database/schema.hpp"

#include <glog/logging.h>

#include <sqlite3.h>

namespace pxd
{

/**
 * Database instance that uses an SQLiteGame instance for everything.
 */
class SQLiteGameDatabase : public Database
{

private:

  /** The underlying SQLiteGame instance.  */
  PXLogic& game;

protected:

  sqlite3_stmt*
  PrepareStatement (const std::string& sql) override
  {
    return game.PrepareStatement (sql);
  }

public:

  explicit SQLiteGameDatabase (PXLogic& g)
    : game(g)
  {}

  SQLiteGameDatabase () = delete;
  SQLiteGameDatabase (const SQLiteGameDatabase&) = delete;
  void operator= (const SQLiteGameDatabase&) = delete;

  Database::IdT
  GetNextId () override
  {
    return game.Ids ("pxd").GetNext ();
  }

};

void
PXLogic::SetupSchema (sqlite3* db)
{
  SetupDatabaseSchema (db);
}

void
PXLogic::GetInitialStateBlock (unsigned& height,
                               std::string& hashHex) const
{
  switch (GetChain ())
    {
    case xaya::Chain::MAIN:
      height = 430000;
      hashHex
          = "38aa107ef495c3878b2608ce951eb51ecd49ea4a5e9094201c12a8c0e2561e0c";
      break;

    case xaya::Chain::TEST:
      height = 10000;
      hashHex
          = "73d771be03c37872bc8ccd92b8acb8d7aa3ac0323195006fb3d3476784981a37";
      break;

    case xaya::Chain::REGTEST:
      height = 0;
      hashHex
          = "6f750b36d22f1dc3d0a6e483af45301022646dfc3b3ba2187865f5a7d6d83ab1";
      break;

    default:
      LOG (FATAL) << "Unexpected chain: " << xaya::ChainToString (GetChain ());
    }
}

void
PXLogic::InitialiseState (sqlite3* db)
{
  /* Nothing needs to be done, we just start with an empty database.  */
}

void
PXLogic::UpdateState (sqlite3* db, const Json::Value& blockData)
{
  SQLiteGameDatabase dbObj(*this);
  const Params params(GetChain ());

  MoveProcessor mvProc(dbObj, params);
  mvProc.ProcessAll (blockData["moves"]);
}

Json::Value
PXLogic::GetStateAsJson (sqlite3* db)
{
  SQLiteGameDatabase dbObj(*this);
  return pxd::GameStateToJson (dbObj);
}

} // namespace pxd
