#include "logic.hpp"

#include "combat.hpp"
#include "dynobstacles.hpp"
#include "gamestatejson.hpp"
#include "movement.hpp"
#include "moveprocessor.hpp"
#include "prospecting.hpp"

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

namespace
{

/**
 * Decrements busy blocks for all characters and processes those that have
 * their operation finished.
 */
void
ProcessBusy (Database& db, xaya::Random& rnd,
             const Params& params, const BaseMap& map)
{
  CharacterTable characters(db);
  RegionsTable regions(db);

  auto res = characters.QueryBusyDone ();
  while (res.Step ())
    {
      auto c = characters.GetFromResult (res);
      CHECK_EQ (c->GetBusy (), 1);
      switch (c->GetProto ().busy_case ())
        {
        case proto::Character::kProspection:
          FinishProspecting (*c, db, regions, rnd, params, map);
          break;

        default:
          LOG (FATAL)
              << "Unexpected busy case: " << c->GetProto ().busy_case ();
        }

      CHECK_EQ (c->GetBusy (), 0);
    }

  characters.DecrementBusy ();
}

} // anonymous namespace

void
PXLogic::UpdateState (Database& db, xaya::Random& rnd,
                      const Params& params, const BaseMap& map,
                      const Json::Value& blockData)
{
  const auto& blockMeta = blockData["block"];
  CHECK (blockMeta.isObject ());
  const auto& heightVal = blockMeta["height"];
  CHECK (heightVal.isUInt64 ());
  const unsigned height = heightVal.asUInt64 ();

  FameUpdater fame(db, height);
  UpdateState (db, fame, rnd, params, map, blockData);
}

void
PXLogic::UpdateState (Database& db, FameUpdater& fame, xaya::Random& rnd,
                      const Params& params, const BaseMap& map,
                      const Json::Value& blockData)
{
  fame.GetDamageLists ().RemoveOld (params.DamageListBlocks ());

  AllHpUpdates (db, fame, rnd, map);
  ProcessBusy (db, rnd, params, map);

  DynObstacles dyn(db);
  MoveProcessor mvProc(db, dyn, rnd, params, map);
  mvProc.ProcessAdmin (blockData["admin"]);
  mvProc.ProcessAll (blockData["moves"]);

  ProcessAllMovement (db, dyn, params, map.GetEdgeWeights ());
  FindCombatTargets (db, rnd);
}

void
PXLogic::SetupSchema (sqlite3* db)
{
  SetupDatabaseSchema (db);
}

void
PXLogic::GetInitialStateBlock (unsigned& height,
                               std::string& hashHex) const
{
  const xaya::Chain chain = GetChain ();
  switch (chain)
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
      LOG (FATAL) << "Unexpected chain: " << xaya::ChainToString (chain);
    }
}

void
PXLogic::InitialiseState (sqlite3* db)
{
  SQLiteGameDatabase dbObj(*this);
  const Params params(GetChain ());

  InitialisePrizes (dbObj, params);
}

void
PXLogic::UpdateState (sqlite3* db, const Json::Value& blockData)
{
  SQLiteGameDatabase dbObj(*this);
  const Params params(GetChain ());

  UpdateState (dbObj, GetContext ().GetRandom (), params, map, blockData);
}

Json::Value
PXLogic::GetStateAsJson (sqlite3* db)
{
  SQLiteGameDatabase dbObj(*this);
  const Params params(GetChain ());

  GameStateJson converter(dbObj, params, map);
  return converter.FullState ();
}

} // namespace pxd
