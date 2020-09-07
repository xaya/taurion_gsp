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

#ifndef PXD_PXRPCSERVER_HPP
#define PXD_PXRPCSERVER_HPP

#include "rpc-stubs/nonstaterpcserverstub.h"
#include "rpc-stubs/pxrpcserverstub.h"

#include "dynobstacles.hpp"
#include "logic.hpp"

#include "mapdata/basemap.hpp"

#include <xayagame/game.hpp>

#include <json/json.h>
#include <jsonrpccpp/server.h>

#include <map>
#include <mutex>

namespace pxd
{

/**
 * Fake JSON-RPC server connector that just does nothing.  By using this class,
 * we can construct instances of the libjson-rpc-cpp servers without needing to
 * really process requests with them (we'll just use it to call the actual
 * methods on it directly).
 */
class NullServerConnector : public jsonrpc::AbstractServerConnector
{

public:

  NullServerConnector () = default;

  bool
  StartListening () override
  {
    return false;
  }

  bool
  StopListening () override
  {
    return false;
  }

};

/**
 * Implementation of RPC methods that do not require a full GSP but instead
 * just operate on e.g. map data.  These methods are exposed locally also
 * for Charon clients (rather than through the server link).
 */
class NonStateRpcServer : public NonStateRpcServerStub
{

private:

  /** The chain this is running on.  */
  const xaya::Chain chain;

  /** The basemap we use.  */
  const BaseMap& map;

  /**
   * Data relevant for findpath about the set of buildings and characters
   * on the map.
   */
  struct PathingData
  {

    /** DynObstacles instance with all those buildings and characters added.  */
    DynObstacles obstacles;

    /**
     * Map from coordinate to the corresponding building ID.  We use that
     * to selectively exclude buildings by ID from the obstacle map, e.g.
     * when pathing "to" a building to enter it.
     */
    std::unordered_map<HexCoord, Database::IdT> buildingIds;

    explicit PathingData (const xaya::Chain c)
      : obstacles(c)
    {}

  };

  /**
   * Building data used for findpath.  This is decoupled from the
   * actual game state, so that it can be done even for Charon clients
   * locally.  It contains a set of buildings, which are specified
   * explicitly by the caller (with a separate RPC and then remembered
   * in the server state at runtime).
   *
   * We use a shared_ptr here so that it can be "copied" when a call is made
   * and then the instance itself does not need to be kept locked while the
   * call is running.
   */
  std::shared_ptr<const PathingData> dyn;

  /** Mutex for protecting dyn in concurrent calls.  */
  std::mutex mutDynObstacles;

  /**
   * Constructs a fresh PathingData instance without any extra
   * buildings added yet.
   */
  std::shared_ptr<PathingData> InitPathingData () const;

  /**
   * Processes a JSON array of building specifications and adds them
   * to the given dynamic obstacle map.  Returns false if something
   * goes wrong, e.g. the JSON format is invalid or some buildings overlap.
   */
  bool AddBuildingsFromJson (const Json::Value& buildings,
                             PathingData& dyn) const;

  /**
   * Processes a JSON array of character data (including at least position
   * and faction), and adds them to the given dynobstacle map.  Returns false
   * if something is wrong (invalid format or characters overlap in an
   * invalid way).
   */
  static bool AddCharactersFromJson (const Json::Value& characters,
                                     PathingData& dyn);

public:

  explicit NonStateRpcServer (jsonrpc::AbstractServerConnector& conn,
                              const BaseMap& m, const xaya::Chain c);

  bool setpathdata (const Json::Value& buildings,
                    const Json::Value& characters) override;
  Json::Value findpath (const Json::Value& exbuildings,
                        const std::string& faction,
                        int l1range, const Json::Value& source,
                        const Json::Value& target) override;
  std::string encodewaypoints (const Json::Value& wp) override;
  Json::Value getregionat (const Json::Value& coord) override;
  Json::Value getbuildingshape (const Json::Value& centre, int rot,
                                const std::string& type) override;
  Json::Value getversion () override;

};

/**
 * Implementation of the JSON-RPC interface to the game daemon.  This mostly
 * contains methods that query the game-state database in some way as needed
 * by the UI process.
 */
class PXRpcServer : public PXRpcServerStub
{

private:

  /** The underlying Game instance that manages everything.  */
  xaya::Game& game;

  /** The PX game logic implementation.  */
  PXLogic& logic;

  /** Null connector for the nonstate instance.  */
  NullServerConnector nullConnector;

  /** NonStateRpcServer for answering the calls it supports.  */
  NonStateRpcServer nonstate;

public:

  explicit PXRpcServer (xaya::Game& g, PXLogic& l,
                        jsonrpc::AbstractServerConnector& conn)
    : PXRpcServerStub(conn), game(g), logic(l),
      nonstate(nullConnector, logic.GetBaseMap (), game.GetChain ())
  {}

  void stop () override;
  Json::Value getcurrentstate () override;
  Json::Value getnullstate () override;
  Json::Value getpendingstate () override;
  std::string waitforchange (const std::string& knownBlock) override;
  Json::Value waitforpendingchange (int oldVersion) override;

  Json::Value getaccounts () override;
  Json::Value getbuildings () override;
  Json::Value getcharacters () override;
  Json::Value getgroundloot () override;
  Json::Value getongoings () override;
  Json::Value getregions (int fromHeight) override;
  Json::Value getmoneysupply () override;
  Json::Value getprizestats () override;

  Json::Value getbootstrapdata () override;

  Json::Value getserviceinfo (const std::string& name,
                              const Json::Value& op) override;

  bool
  setpathdata (const Json::Value& buildings,
               const Json::Value& characters) override
  {
    return nonstate.setpathdata (buildings, characters);
  }

  Json::Value
  findpath (const Json::Value& exbuildings, const std::string& faction,
            int l1range, const Json::Value& source,
            const Json::Value& target) override
  {
    return nonstate.findpath (exbuildings, faction, l1range, source, target);
  }

  std::string
  encodewaypoints (const Json::Value& wp) override
  {
    return nonstate.encodewaypoints (wp);
  }

  Json::Value
  getregionat (const Json::Value& coord) override
  {
    return nonstate.getregionat (coord);
  }

  Json::Value
  getbuildingshape (const Json::Value& centre, const int rot,
                    const std::string& type) override
  {
    return nonstate.getbuildingshape (centre, rot, type);
  }

  Json::Value
  getversion () override
  {
    return nonstate.getversion ();
  }

};

} // namespace pxd

#endif // PXD_PXRPCSERVER_HPP
