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

#include "config.h"

#include "charon.hpp"
#include "logic.hpp"
#include "pending.hpp"
#include "pxrpcserver.hpp"

#include <xayagame/defaultmain.hpp>
#include <xayagame/game.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <jsonrpccpp/server/connectors/httpserver.h>

#include <google/protobuf/stubs/common.h>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{

DEFINE_string (xaya_rpc_url, "",
               "URL at which Xaya Core's JSON-RPC interface is available");
DEFINE_int32 (game_rpc_port, 0,
              "the port at which the game's JSON-RPC server will be started"
              " (if non-zero)");
DEFINE_bool (game_rpc_listen_locally, true,
             "whether the game's JSON-RPC server should listen locally");

DEFINE_int32 (enable_pruning, -1,
              "if non-negative (including zero), old undo data will be pruned"
              " and only as many blocks as specified will be kept");

DEFINE_string (datadir, "",
               "base data directory for game data (will be extended by game ID"
               " and the chain)");

DEFINE_bool (pending_moves, true,
             "whether or not pending moves should be tracked");

class PXInstanceFactory : public xaya::CustomisedInstanceFactory
{

private:

  /**
   * Reference to the PXLogic instance.  This is needed to construct the
   * RPC server.
   */
  pxd::PXLogic& rules;

public:

  explicit PXInstanceFactory (pxd::PXLogic& r)
    : rules(r)
  {}

  std::unique_ptr<xaya::RpcServerInterface>
  BuildRpcServer (xaya::Game& game,
                  jsonrpc::AbstractServerConnector& conn) override
  {
    std::unique_ptr<xaya::RpcServerInterface> res;
    res.reset (new xaya::WrappedRpcServer<pxd::PXRpcServer> (game, rules,
                                                             conn));
    return res;
  }

  std::vector<std::unique_ptr<xaya::GameComponent>>
  BuildGameComponents (xaya::Game& game) override
  {
    std::vector<std::unique_ptr<xaya::GameComponent>> res;

    auto charonSrv = MaybeBuildCharonServer (game, rules);
    if (charonSrv != nullptr)
      res.push_back (std::move (charonSrv));

    return res;
  }

};

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  gflags::SetUsageMessage ("Run Taurion game daemon");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  if (FLAGS_xaya_rpc_url.empty ())
    {
      std::cerr << "Error: --xaya_rpc_url must be set" << std::endl;
      return EXIT_FAILURE;
    }
  if (FLAGS_datadir.empty ())
    {
      std::cerr << "Error: --datadir must be specified" << std::endl;
      return EXIT_FAILURE;
    }

#ifdef ENABLE_SLOW_ASSERTS
  LOG (WARNING)
      << "Slow assertions are enabled.  This is fine for testing, but will"
         " slow down syncing";
#endif // ENABLE_SLOW_ASSERTS

  xaya::GameDaemonConfiguration config;
  config.XayaRpcUrl = FLAGS_xaya_rpc_url;
  if (FLAGS_game_rpc_port != 0)
    {
      config.GameRpcServer = xaya::RpcServerType::HTTP;
      config.GameRpcPort = FLAGS_game_rpc_port;
      config.GameRpcListenLocally = FLAGS_game_rpc_listen_locally;
    }
  config.EnablePruning = FLAGS_enable_pruning;
  config.DataDirectory = FLAGS_datadir;

  /* We need the "proper" implementation of admin commands in the ZMQ
     notifications, which was implemented for 1.3 and up in
     https://github.com/xaya/xaya/pull/93, as well as backported for the
     1.2.0 release.  */
  config.MinXayaVersion = 1020000;

  pxd::PXLogic rules;
  PXInstanceFactory instanceFact(rules);
  config.InstanceFactory = &instanceFact;

  pxd::PendingMoves pending(rules);
  if (FLAGS_pending_moves)
    config.PendingMoves = &pending;

  const int rc = xaya::SQLiteMain (config, "tn", rules);

  google::protobuf::ShutdownProtobufLibrary ();
  return rc;
}
