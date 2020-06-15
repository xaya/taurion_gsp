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

#include "pxrpcserver.hpp"

#include "buildings.hpp"
#include "jsonutils.hpp"
#include "movement.hpp"
#include "services.hpp"

#include "database/itemcounts.hpp"
#include "proto/roconfig.hpp"

#include <xayagame/gamerpcserver.hpp>

#include <glog/logging.h>

#include <limits>
#include <sstream>
#include <string>

namespace pxd
{

/* ************************************************************************** */

namespace
{

/** Maximum number of past blocks for which getregions can be called.  */
constexpr int MAX_REGIONS_HEIGHT_DIFFERENCE = 2 * 60 * 24 * 3;

/**
 * Error codes returned from the PX RPC server.  All values should have an
 * explicit integer number, because this also defines the RPC protocol
 * itself for clients that do not have access to the ErrorCode enum
 * directly and only read the integer values.
 */
enum class ErrorCode
{

  /* Invalid values for arguments (e.g. passing a malformed JSON value for
     a HexCoord or an out-of-range integer.  */
  INVALID_ARGUMENT = -1,

  /* Non-existing account passed as associated name for some RPC.  */
  INVALID_ACCOUNT = -2,

  /* Specific errors with findpath.  */
  FINDPATH_NO_CONNECTION = 1,

  /* Specific errors with getregionat.  */
  REGIONAT_OUT_OF_MAP = 2,

  /* Specific errors with getregions.  */
  GETREGIONS_FROM_TOO_LOW = 3,

};

/**
 * Throws a JSON-RPC error from the current method.  This throws an exception,
 * so does not return to the caller in a normal way.
 */
void
ReturnError (const ErrorCode code, const std::string& msg)
{
  throw jsonrpc::JsonRpcException (static_cast<int> (code), msg);
}

/**
 * Checks that a given integer is within the given bounds.  Otherwise
 * returns an INVALID_ARGUMENT error.  Both bounds are inclusive.
 */
void
CheckIntBounds (const std::string& name, const int value,
                const int min, const int max)
{
  if (value >= min && value <= max)
    return;

  std::ostringstream msg;
  msg << name << " is out of bounds (" << value << " is not within "
      << min << " and " << max << ")";
  ReturnError (ErrorCode::INVALID_ARGUMENT, msg.str ());
}

} // anonymous namespace

/* ************************************************************************** */

NonStateRpcServer::NonStateRpcServer (jsonrpc::AbstractServerConnector& conn,
                                      const BaseMap& m, const xaya::Chain c)
  : NonStateRpcServerStub(conn), chain(c), map(m)
{
  std::lock_guard<std::mutex> lock(mutDynObstacles);
  InitDynObstacles ();
}

void
NonStateRpcServer::InitDynObstacles ()
{
  dyn = std::make_shared<DynObstacles> (chain);
}

Json::Value
NonStateRpcServer::findpath (const int l1range, const Json::Value& source,
                             const Json::Value& target, const int wpdist)
{
  LOG (INFO)
      << "RPC method called: findpath\n"
      << "  l1range=" << l1range << ", wpdist=" << wpdist << ",\n"
      << "  source=" << source << ",\n"
      << "  target=" << target;

  HexCoord sourceCoord;
  if (!CoordFromJson (source, sourceCoord))
    ReturnError (ErrorCode::INVALID_ARGUMENT,
                 "source is not a valid coordinate");

  HexCoord targetCoord;
  if (!CoordFromJson (target, targetCoord))
    ReturnError (ErrorCode::INVALID_ARGUMENT,
                 "target is not a valid coordinate");

  const int maxInt = std::numeric_limits<HexCoord::IntT>::max ();
  CheckIntBounds ("l1range", l1range, 0, maxInt);
  CheckIntBounds ("wpdist", wpdist, 1, maxInt);

  /* We do not want to keep a lock on the dyn mutex while the potentially
     long call is running.  Instead, we just copy the shared pointer and
     then release the lock again.  Once created, the DynObstacle instance
     (inside the shared pointer) is immutable, so this is safe.  */
  std::shared_ptr<const DynObstacles> dynCopy;
  {
    std::lock_guard<std::mutex> lock(mutDynObstacles);
    dynCopy = dyn;
  }
  CHECK (dynCopy != nullptr);

  PathFinder finder(targetCoord);
  const auto edges = [this, &dynCopy] (const HexCoord& from, const HexCoord& to)
    {
      /* The faction here does not matter, as we only have buildings
         in the DynObstacles anyway (if at all).  We just need to pass
         in some faction.  */
      return MovementEdgeWeight (map, *dynCopy, Faction::RED, from, to);
    };
  const PathFinder::DistanceT dist = finder.Compute (edges, sourceCoord,
                                                     l1range);

  if (dist == PathFinder::NO_CONNECTION)
    ReturnError (ErrorCode::FINDPATH_NO_CONNECTION,
                 "no connection between source and target"
                 " within the given l1range");

  Json::Value wp(Json::arrayValue);
  PathFinder::Stepper path = finder.StepPath (sourceCoord);
  HexCoord lastWp = path.GetPosition ();
  wp.append (CoordToJson (lastWp));
  for (; path.HasMore (); path.Next ())
    {
      if (HexCoord::DistanceL1 (lastWp, path.GetPosition ()) >= wpdist)
        {
          lastWp = path.GetPosition ();
          wp.append (CoordToJson (lastWp));
        }
    }
  if (lastWp != path.GetPosition ())
    wp.append (CoordToJson (path.GetPosition ()));

  Json::Value res(Json::objectValue);
  res["dist"] = dist;
  res["wp"] = wp;

  return res;
}

Json::Value
NonStateRpcServer::getregionat (const Json::Value& coord)
{
  LOG (INFO)
      << "RPC method called: getregionat\n"
      << "  coord=" << coord;

  HexCoord c;
  if (!CoordFromJson (coord, c))
    ReturnError (ErrorCode::INVALID_ARGUMENT,
                 "coord is not a valid coordinate");

  if (!map.IsOnMap (c))
    ReturnError (ErrorCode::REGIONAT_OUT_OF_MAP,
                 "coord is outside the game map");

  RegionMap::IdT id;
  const auto tiles = map.Regions ().GetRegionShape (c, id);

  Json::Value tilesArr(Json::arrayValue);
  for (const auto& c : tiles)
    tilesArr.append (CoordToJson (c));

  Json::Value res(Json::objectValue);
  res["id"] = id;
  res["tiles"] = tilesArr;

  return res;
}

Json::Value
NonStateRpcServer::getbuildingshape (const Json::Value& centre, const int rot,
                                     const std::string& type)
{
  LOG (INFO)
      << "RPC method called: getbuildingshape " << type << "\n"
      << "  centre=" << centre << "\n"
      << "  rot=" << rot;

  HexCoord c;
  if (!CoordFromJson (centre, c))
    ReturnError (ErrorCode::INVALID_ARGUMENT,
                 "centre is not a valid coordinate");

  if (rot < 0 || rot >= 6)
    ReturnError (ErrorCode::INVALID_ARGUMENT,
                 "rot is outside the valid range [0, 5]");

  if (RoConfig (chain).BuildingOrNull (type) == nullptr)
    ReturnError (ErrorCode::INVALID_ARGUMENT, "unknown building type");

  proto::ShapeTransformation trafo;
  trafo.set_rotation_steps (rot);

  Json::Value res(Json::arrayValue);
  for (const auto& t : GetBuildingShape (type, trafo, c, chain))
    res.append (CoordToJson (t));

  return res;
}

/* ************************************************************************** */

void
PXRpcServer::stop ()
{
  LOG (INFO) << "RPC method called: stop";
  game.RequestStop ();
}

Json::Value
PXRpcServer::getcurrentstate ()
{
  LOG (INFO) << "RPC method called: getcurrentstate";
  return game.GetCurrentJsonState ();
}

Json::Value
PXRpcServer::getnullstate ()
{
  LOG (INFO) << "RPC method called: getnullstate";
  return game.GetNullJsonState ();
}

Json::Value
PXRpcServer::getpendingstate ()
{
  LOG (INFO) << "RPC method called: getpendingstate";
  return game.GetPendingJsonState ();
}

Json::Value
PXRpcServer::waitforpendingchange (const int oldVersion)
{
  LOG (INFO) << "RPC method called: waitforpendingchange " << oldVersion;
  return game.WaitForPendingChange (oldVersion);
}

std::string
PXRpcServer::waitforchange (const std::string& knownBlock)
{
  LOG (INFO) << "RPC method called: waitforchange " << knownBlock;
  return xaya::GameRpcServer::DefaultWaitForChange (game, knownBlock);
}

Json::Value
PXRpcServer::getaccounts ()
{
  LOG (INFO) << "RPC method called: getaccounts";
  return logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.Accounts ();
      });
}

Json::Value
PXRpcServer::getbuildings ()
{
  LOG (INFO) << "RPC method called: getbuildings";
  return logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.Buildings ();
      });
}

Json::Value
PXRpcServer::getcharacters ()
{
  LOG (INFO) << "RPC method called: getcharacters";
  return logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.Characters ();
      });
}

Json::Value
PXRpcServer::getgroundloot ()
{
  LOG (INFO) << "RPC method called: getgroundloot";
  return logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.GroundLoot ();
      });
}

Json::Value
PXRpcServer::getongoings ()
{
  LOG (INFO) << "RPC method called: getongoings";
  return logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.OngoingOperations ();
      });
}

Json::Value
PXRpcServer::getregions (const int fromHeight)
{
  LOG (INFO) << "RPC method called: getregions " << fromHeight;

  return logic.GetCustomStateData (game,
    [fromHeight] (GameStateJson& gsj, const xaya::uint256 hash,
                  const int height)
      {
        if (fromHeight + MAX_REGIONS_HEIGHT_DIFFERENCE < height)
          {
            std::ostringstream msg;
            msg << "fromHeight " << fromHeight
                << " is too low for current block height " << height
                << ", needs to be at least "
                << height - MAX_REGIONS_HEIGHT_DIFFERENCE;
            ReturnError (ErrorCode::GETREGIONS_FROM_TOO_LOW, msg.str ());
          }

        return gsj.Regions (fromHeight);
      });
}

Json::Value
PXRpcServer::getprizestats ()
{
  LOG (INFO) << "RPC method called: getprizestats";
  return logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.PrizeStats ();
      });
}

Json::Value
PXRpcServer::getbootstrapdata ()
{
  LOG (INFO) << "RPC method called: getbootstrapdata";
  return logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.BootstrapData ();
      });
}

Json::Value
PXRpcServer::getserviceinfo (const std::string& name, const Json::Value& op)
{
  LOG (INFO) << "RPC method called: getserviceinfo " << name << "\n" << op;
  return logic.GetCustomStateData (game,
    [&] (Database& db, const xaya::uint256& hash, const unsigned height)
    {
      const Context ctx(logic.GetChain (), logic.GetBaseMap (),
                        height + 1, Context::NO_TIMESTAMP);

      AccountsTable accounts(db);
      BuildingsTable buildings(db);
      BuildingInventoriesTable inv(db);
      CharacterTable characters(db);
      ItemCounts cnt(db);
      OngoingsTable ong(db);

      const auto acc = accounts.GetByName (name);
      if (acc == nullptr)
        {
          std::ostringstream msg;
          msg << "account does not exist: " << name;
          ReturnError (ErrorCode::INVALID_ACCOUNT, msg.str ());
        }

      const auto parsed = ServiceOperation::Parse (*acc, op, ctx,
                                                   accounts, buildings, inv,
                                                   characters, cnt, ong);
      if (parsed == nullptr)
        return Json::Value ();

      Json::Value res = parsed->ToPendingJson ();
      CHECK (res.isObject ());
      res["valid"] = parsed->IsFullyValid ();

      return res;
    });
}

/* ************************************************************************** */

} // namespace pxd
