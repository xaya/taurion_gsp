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

#include "pxrpcserver.hpp"

#include "jsonutils.hpp"

#include <xayagame/gamerpcserver.hpp>

#include <glog/logging.h>

#include <limits>
#include <sstream>
#include <string>

namespace pxd
{

namespace
{

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

  /* Specific errors with findpath.  */
  FINDPATH_NO_CONNECTION = 1,

  /* Specific errors with getregionat.  */
  REGIONAT_OUT_OF_MAP = 2,

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
  return logic.GetCustomStateData (game, "data",
    [] (sqlite3* db)
      {
        return Json::Value ();
      });
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
PXRpcServer::findpath (const int l1range, const Json::Value& source,
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

  PathFinder finder(logic.map.GetEdgeWeights (), targetCoord);
  const PathFinder::DistanceT dist = finder.Compute (sourceCoord, l1range);

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
PXRpcServer::getregionat (const Json::Value& coord)
{
  LOG (INFO)
      << "RPC method called: getregionat\n"
      << "  coord=" << coord;

  HexCoord c;
  if (!CoordFromJson (coord, c))
    ReturnError (ErrorCode::INVALID_ARGUMENT,
                 "coord is not a valid coordinate");

  if (!logic.map.IsOnMap (c))
    ReturnError (ErrorCode::REGIONAT_OUT_OF_MAP,
                 "coord is outside the game map");

  RegionMap::IdT id;
  const auto tiles = logic.map.Regions ().GetRegionShape (c, id);

  Json::Value tilesArr(Json::arrayValue);
  for (const auto& c : tiles)
    tilesArr.append (CoordToJson (c));

  Json::Value res(Json::objectValue);
  res["id"] = id;
  res["tiles"] = tilesArr;

  return res;
}

} // namespace pxd
