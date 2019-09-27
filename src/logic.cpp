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

#include "logic.hpp"

#include "combat.hpp"
#include "dynobstacles.hpp"
#include "movement.hpp"
#include "moveprocessor.hpp"
#include "prospecting.hpp"

#include "database/account.hpp"
#include "database/schema.hpp"

#include <glog/logging.h>

namespace pxd
{

sqlite3_stmt*
SQLiteGameDatabase::PrepareStatement (const std::string& sql)
{
  return game.PrepareStatement (sql);
}

Database::IdT
SQLiteGameDatabase::GetNextId ()
{
  return game.Ids ("pxd").GetNext ();
}

namespace
{

/**
 * Decrements busy blocks for all characters and processes those that have
 * their operation finished.
 */
void
ProcessBusy (Database& db, xaya::Random& rnd,
             const int64_t timestamp, const Params& params, const BaseMap& map)
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
          FinishProspecting (*c, db, regions, rnd, timestamp, params, map);
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
  const auto& block = blockData["block"];
  CHECK (block.isObject ());
  const auto& timestampVal = block["timestamp"];
  CHECK (timestampVal.isInt64 ());
  const int64_t timestamp = timestampVal.asInt64 ();

  fame.GetDamageLists ().RemoveOld (params.DamageListBlocks ());

  AllHpUpdates (db, fame, rnd, map);
  ProcessBusy (db, rnd, timestamp, params, map);

  DynObstacles dyn(db);
  MoveProcessor mvProc(db, dyn, rnd, params, map);
  mvProc.ProcessAdmin (blockData["admin"]);
  mvProc.ProcessAll (blockData["moves"]);

  ProcessAllMovement (db, dyn, params, map);
  FindCombatTargets (db, rnd);

#ifdef ENABLE_SLOW_ASSERTS
  ValidateStateSlow (db);
#endif // ENABLE_SLOW_ASSERTS
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
      height = 1150360;
      hashHex
          = "bae54563984b22b7af7c17f362988dede48d51dd5840c64bc532bc596f07c98d";
      break;

    case xaya::Chain::TEST:
      height = 63600;
      hashHex
          = "722011c75e54d6a5f2539c2911a80c25c119df9db3328a92821309b96b6dc93d";
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
  GameStateJson gsj(dbObj, params, map);

  return gsj.FullState ();
}

Json::Value
PXLogic::GetCustomStateData (xaya::Game& game, const JsonStateFromDatabase& cb)
{
  return SQLiteGame::GetCustomStateData (game, "data",
      [this, &cb] (sqlite3* db)
        {
          SQLiteGameDatabase dbObj(*this);
          const Params params(GetChain ());
          GameStateJson gsj(dbObj, params, map);

          return cb (gsj);
        });
}

namespace
{

/**
 * Verifies that each character's faction in the database matches the
 * owner's faction.
 */
void
ValidateCharacterFactions (Database& db)
{
  std::unordered_map<std::string, Faction> accountFactions;
  {
    AccountsTable accounts(db);
    auto res = accounts.QueryInitialised ();
    while (res.Step ())
      {
        auto a = accounts.GetFromResult (res);
        auto insert = accountFactions.emplace (a->GetName (), a->GetFaction ());
        CHECK (insert.second) << "Duplicate account name " << a->GetName ();
      }
  }

  CharacterTable characters(db);
  auto res = characters.QueryAll ();
  while (res.Step ())
    {
      auto c = characters.GetFromResult (res);
      const auto mit = accountFactions.find (c->GetOwner ());
      CHECK (mit != accountFactions.end ())
          << "Character " << c->GetId ()
          << " owned by uninitialised account " << c->GetOwner ();
      CHECK (c->GetFaction () == mit->second)
          << "Faction mismatch between character " << c->GetId ()
          << " and owner account " << c->GetOwner ();
    }
}

} // anonymous namespace

void
PXLogic::ValidateStateSlow (Database& db)
{
  LOG (INFO) << "Performing slow validation of the game-state database...";
  ValidateCharacterFactions (db);
}

} // namespace pxd
