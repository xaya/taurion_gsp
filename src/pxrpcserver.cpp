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
  FINDPATH_ENCODE_FAILED = 4,

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
  dyn = InitPathingData ();
}

std::shared_ptr<NonStateRpcServer::PathingData>
NonStateRpcServer::InitPathingData () const
{
  return std::make_shared<PathingData> (chain);
}

bool
NonStateRpcServer::AddBuildingsFromJson (const Json::Value& buildings,
                                         PathingData& dyn) const
{
  /* This is enforced already by libjson-rpc-cpp's stub generator.  */
  CHECK (buildings.isArray ());

  const RoConfig cfg(chain);
  for (const auto& b : buildings)
    {
      if (!b.isObject ())
        return false;

      Database::IdT id;
      if (!IdFromJson (b["id"], id))
        return false;

      const auto& typeVal = b["type"];
      if (!typeVal.isString ())
        return false;
      const std::string type = typeVal.asString ();
      if (cfg.BuildingOrNull (type) == nullptr)
        return false;

      const auto& rotVal = b["rotationsteps"];
      if (!rotVal.isInt64 ())
        return false;
      const int rot = rotVal.asInt64 ();
      if (rot < 0 || rot > 5)
        return false;
      proto::ShapeTransformation trafo;
      trafo.set_rotation_steps (rot);

      HexCoord centre;
      if (!CoordFromJson (b["centre"], centre))
        return false;

      std::vector<HexCoord> shape;
      if (!dyn.obstacles.AddBuilding (type, trafo, centre, shape))
        {
          LOG (WARNING) << "Adding the building failed\n" << b;
          return false;
        }

      for (const auto& tile : shape)
        CHECK (dyn.buildingIds.emplace (tile, id).second);
    }

  return true;
}

bool
NonStateRpcServer::AddCharactersFromJson (const Json::Value& characters,
                                          PathingData& dyn)
{
  /* This is enforced already by libjson-rpc-cpp's stub generator.  */
  CHECK (characters.isArray ());

  for (const auto& c : characters)
    {
      if (!c.isObject ())
        return false;

      /* If the character is in a building, we just ignore them rather than
         failing for missing "position".  */
      if (c.isMember ("inbuilding"))
        continue;

      HexCoord pos;
      if (!CoordFromJson (c["position"], pos))
        return false;

      const auto& factVal = c["faction"];
      if (!factVal.isString ())
        return false;
      const Faction faction = FactionFromString (factVal.asString ());
      switch (faction)
        {
        case Faction::INVALID:
        case Faction::ANCIENT:
          return false;

        case Faction::RED:
        case Faction::GREEN:
        case Faction::BLUE:
          break;

        default:
          LOG (FATAL) << "Invalid faction: " << static_cast<int> (faction);
          break;
        }

      if (!dyn.obstacles.AddVehicle (pos, faction))
        return false;
    }

  return true;
}

bool
NonStateRpcServer::setpathdata (const Json::Value& buildings,
                                const Json::Value& characters)
{
  LOG (INFO) << "RPC method called: setpathdata";
  VLOG (1) << "  Buildings data:\n" << buildings;
  VLOG (1) << "  Character data:\n" << characters;

  /* We first construct the full obstacle map, and only lock the mutex
     later on when replacing the pointer in the instance.  This avoids
     locking for a longer time while processing the buildings.  */

  auto fresh = InitPathingData ();
  if (!AddBuildingsFromJson (buildings, *fresh))
    ReturnError (ErrorCode::INVALID_ARGUMENT, "buildings is invalid");
  if (!AddCharactersFromJson (characters, *fresh))
    ReturnError (ErrorCode::INVALID_ARGUMENT, "characters is invalid");

  {
    std::lock_guard<std::mutex> lock(mutDynObstacles);
    dyn = std::move (fresh);
  }

  /* The return value does not really mean anything.  But we can't nicely
     tell libjson-rpc-cpp that the method returns null, and we can't make it
     into a notification either, as the caller might want feedback on when
     processing is done.  */
  return true;
}

Json::Value
NonStateRpcServer::findpath (const Json::Value& exbuildings,
                             const std::string& faction,
                             const int l1range, const Json::Value& source,
                             const Json::Value& target)
{
  LOG (INFO)
      << "RPC method called: findpath\n"
      << "  l1range=" << l1range << ", faction=" << faction << "\n"
      << "  source=" << source << ",\n"
      << "  target=" << target << ",\n"
      << "  exbuildings=" << exbuildings;

  HexCoord sourceCoord;
  if (!CoordFromJson (source, sourceCoord))
    ReturnError (ErrorCode::INVALID_ARGUMENT,
                 "source is not a valid coordinate");

  HexCoord targetCoord;
  if (!CoordFromJson (target, targetCoord))
    ReturnError (ErrorCode::INVALID_ARGUMENT,
                 "target is not a valid coordinate");

  const Faction f = FactionFromString (faction);
  switch (f)
    {
    case Faction::INVALID:
    case Faction::ANCIENT:
      ReturnError (ErrorCode::INVALID_ARGUMENT, "faction is invalid");

    case Faction::RED:
    case Faction::GREEN:
    case Faction::BLUE:
      break;

    default:
      LOG (FATAL) << "Unexpected faction: " << static_cast<int> (f);
      break;
    }

  const int maxInt = std::numeric_limits<HexCoord::IntT>::max ();
  CheckIntBounds ("l1range", l1range, 0, maxInt);

  std::unordered_set<Database::IdT> exBuildingIds;
  CHECK (exbuildings.isArray ());
  for (const auto& entry : exbuildings)
    {
      Database::IdT id;
      if (!IdFromJson (entry, id))
        ReturnError (ErrorCode::INVALID_ARGUMENT, "exbuildings is not valid");
      exBuildingIds.insert (id);
    }

  /* We do not want to keep a lock on the dyn mutex while the potentially
     long call is running.  Instead, we just copy the shared pointer and
     then release the lock again.  Once created, the DynObstacle instance
     (inside the shared pointer) is immutable, so this is safe.  */
  std::shared_ptr<const PathingData> dynCopy;
  {
    std::lock_guard<std::mutex> lock(mutDynObstacles);
    dynCopy = dyn;
  }
  CHECK (dynCopy != nullptr);

  PathFinder finder(targetCoord);
  const auto edges = [this, f, &dynCopy, &exBuildingIds] (const HexCoord& from,
                                                          const HexCoord& to)
    {
      const auto base = MovementEdgeWeight (map, f, from, to);
      if (base == PathFinder::NO_CONNECTION)
        return PathFinder::NO_CONNECTION;

      /* If the path is blocked by a building, look closer to see if it is one
         of the buildings we want to ignore or not.  */
      if (dynCopy->obstacles.IsBuilding (to))
        {
          const auto mitTiles = dynCopy->buildingIds.find (to);
          if (mitTiles == dynCopy->buildingIds.end ()
                || exBuildingIds.count (mitTiles->second) == 0)
            return PathFinder::NO_CONNECTION;
        }

      if (dynCopy->obstacles.HasVehicle (to, f))
        return PathFinder::NO_CONNECTION;

      return base;
    };
  const PathFinder::DistanceT dist = finder.Compute (edges, sourceCoord,
                                                     l1range);

  if (dist == PathFinder::NO_CONNECTION)
    ReturnError (ErrorCode::FINDPATH_NO_CONNECTION,
                 "no connection between source and target"
                 " within the given l1range");

  /* Now step the path and construct waypoints, so that it is a principal
     direction between each of them.  */
  std::vector<HexCoord> wp;
  PathFinder::Stepper path = finder.StepPath (sourceCoord);
  wp.push_back (path.GetPosition ());
  HexCoord prev = wp.back ();
  while (path.HasMore ())
    {
      path.Next ();

      HexCoord dir;
      HexCoord::IntT steps;
      if (!wp.back ().IsPrincipalDirectionTo (path.GetPosition (), dir, steps))
        wp.push_back (prev);

      prev = path.GetPosition ();
    }
  if (wp.back () != path.GetPosition ())
    wp.push_back (path.GetPosition ());

  Json::Value jsonWp;
  std::string encoded;
  if (!EncodeWaypoints (wp, jsonWp, encoded))
    ReturnError (ErrorCode::FINDPATH_ENCODE_FAILED,
                 "could not encode waypoints");

  Json::Value res(Json::objectValue);
  res["dist"] = dist;
  res["wp"] = jsonWp;
  res["encoded"] = encoded;

  return res;
}

std::string
NonStateRpcServer::encodewaypoints (const Json::Value& wp)
{
  LOG (INFO) << "RPC method called: encodewaypoints\n" << wp;

  CHECK (wp.isArray ());

  std::vector<HexCoord> wpArr;
  for (const auto& entry : wp)
    {
      HexCoord c;
      if (!CoordFromJson (entry, c))
        ReturnError (ErrorCode::INVALID_ARGUMENT, "invalid waypoints");
      wpArr.push_back (c);
    }

  Json::Value jsonWp;
  std::string encoded;
  if (!EncodeWaypoints (wpArr, jsonWp, encoded))
    ReturnError (ErrorCode::FINDPATH_ENCODE_FAILED,
                 "could not encode waypoints");

  return encoded;
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
PXRpcServer::getmoneysupply ()
{
  LOG (INFO) << "RPC method called: getmoneysupply";
  return logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.MoneySupply ();
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
