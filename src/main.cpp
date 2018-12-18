#include "config.h"

#include "logic.hpp"
#include "pxrpcserver.hpp"

#include <xayagame/game.hpp>
#include <xayagame/gamelogic.hpp>

#include <jsonrpccpp/client/connectors/httpclient.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <experimental/filesystem>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

DEFINE_string (xaya_rpc_url, "",
               "URL at which Xaya Core's JSON-RPC interface is available");
DEFINE_int32 (game_rpc_port, 0,
              "the port at which the game's JSON-RPC server will be started"
              " (if non-zero)");

DEFINE_int32 (enable_pruning, -1,
              "if non-negative (including zero), old undo data will be pruned"
              " and only as many blocks as specified will be kept");

DEFINE_string (datadir, "",
               "base data directory for game data (will be extended by game ID"
               " and the chain)");

namespace fs = std::experimental::filesystem;

constexpr const char GAME_ID[] = "px";

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run Project X game daemon");
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

  try
    {
      jsonrpc::HttpClient httpConnector(FLAGS_xaya_rpc_url);
      auto game = std::make_unique<xaya::Game> (GAME_ID);
      game->ConnectRpcClient (httpConnector);
      CHECK (game->DetectZmqEndpoint ());

      const fs::path gameDir
          = fs::path (FLAGS_datadir)
              / fs::path (GAME_ID)
              / fs::path (xaya::ChainToString (game->GetChain ()));
      if (fs::is_directory (gameDir))
        LOG (INFO) << "Using existing data directory: " << gameDir;
      else
        {
          LOG (INFO) << "Creating data directory: " << gameDir;
          CHECK (fs::create_directories (gameDir));
        }
      const fs::path dbFile = gameDir / fs::path ("storage.sqlite");

      pxd::PXLogic logic(dbFile.string ());
      game->SetStorage (logic.GetStorage ());
      game->SetGameLogic (&logic);

      if (FLAGS_enable_pruning >= 0)
        game->EnablePruning (FLAGS_enable_pruning);

      std::unique_ptr<jsonrpc::AbstractServerConnector> serverConnector;
      std::unique_ptr<pxd::PXRpcServer> rpcServer;
      if (FLAGS_game_rpc_port != 0)
        {
          LOG (INFO)
              << "Starting the SMC JSON-RPC interface on port: "
              << FLAGS_game_rpc_port;
          serverConnector
              = std::make_unique<jsonrpc::HttpServer> (FLAGS_game_rpc_port);
        }
      if (serverConnector == nullptr)
        LOG (WARNING) << "No JSON-RPC server is configured for SMC";
      else
        rpcServer
            = std::make_unique<pxd::PXRpcServer> (*game, logic,
                                                  *serverConnector);

      if (rpcServer != nullptr)
        rpcServer->StartListening ();
      game->Run ();
      if (rpcServer != nullptr)
        rpcServer->StopListening ();

      /* Make sure that the Game instance is destructed before the storage
         (part of the SMCLogic) is, so that potentially last cached DB
         transactions are flushed before the storage goes out of scope.  */
      game.reset ();
    }
  catch (const std::exception& exc)
    {
      LOG (FATAL) << "Exception caught: " << exc.what ();
    }
  catch (...)
    {
      LOG (FATAL) << "Unknown exception caught";
    }

  return EXIT_SUCCESS;
}
