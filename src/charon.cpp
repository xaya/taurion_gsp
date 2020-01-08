/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "charon.hpp"

#include "pxrpcserver.hpp"

#include <charon/notifications.hpp>
#include <charon/rpcserver.hpp>
#include <charon/server.hpp>
#include <charon/waiterthread.hpp>

#include <json/json.h>
#include <jsonrpccpp/common/errors.h>
#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server/abstractserverconnector.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <map>
#include <string>

namespace pxd
{

namespace
{

DEFINE_string (charon, "",
               "Whether to run a Charon server (\"server\"),"
               " client (\"client\") or nothing (default)");

DEFINE_string (charon_server_jid, "", "Bare or full JID for the Charon server");
DEFINE_string (charon_password, "", "XMPP password for the Charon JID");
DEFINE_int32 (charon_priority, 0, "Priority for the XMPP connection");

DEFINE_string (charon_pubsub_service, "",
               "The pubsub service to use on the Charon server");

/** Method pointer to a PXRpcServer method.  */
using PXRpcMethod = void (PXRpcServer::*) (const Json::Value&, Json::Value&);

/** Methods that are forwarded through Charon.  */
const std::map<std::string, PXRpcMethod> CHARON_METHODS = {
  {"getnullstate", &PXRpcServer::getnullstateI},
  {"getpendingstate", &PXRpcServer::getpendingstateI},
  {"getaccounts", &PXRpcServer::getaccountsI},
  {"getcharacters", &PXRpcServer::getcharactersI},
  {"getgroundloot", &PXRpcServer::getgroundlootI},
  {"getregions", &PXRpcServer::getregionsI},
  {"getprizestats", &PXRpcServer::getprizestatsI},
};

/**
 * UpdateWaiter implementation that forwards wait calls to a given call
 * on a PXRpcServer instance.
 */
class UpdateWaiter : public charon::UpdateWaiter
{

private:

  /** PXRpcServer instance to call wait methods on.  */
  PXRpcServer& rpc;

  /** The method to actually call.  */
  const PXRpcMethod method;

  /** The argument list to pass.  */
  Json::Value params;

public:

  explicit UpdateWaiter (PXRpcServer& r, const PXRpcMethod m,
                         const Json::Value& alwaysBlock)
    : rpc(r), method(m), params(Json::arrayValue)
  {
    params.append (alwaysBlock);
  }

  bool
  WaitForUpdate (Json::Value& newState) override
  {
    (rpc.*method) (params, newState);
    return true;
  }

};

/**
 * Charon backend implementation that answers method calls directly through
 * a Game instance (without going through some JSON-RPC loop).
 */
class CharonBackend : public xaya::GameComponent, private charon::RpcServer
{

private:

  /** Fake server connector we use.  */
  NullServerConnector conn;

  /** Underlying PXRpcServer that we call through on.  */
  PXRpcServer rpc;

  /** The Charon server that we use.  */
  charon::Server srv;

  /**
   * Enables a notification waiter on the server.  The waiter will just
   * use the given notification type and call through to a method on
   * our PXRpcServer instance.
   */
  void
  AddNotification (std::unique_ptr<charon::NotificationType> n,
                   const PXRpcMethod m)
  {
    auto w = std::make_unique<UpdateWaiter> (rpc, m, n->AlwaysBlockId ());
    auto t = std::make_unique<charon::WaiterThread> (std::move (n),
                                                     std::move (w));
    srv.AddNotification (std::move (t));
  }

  Json::Value HandleMethod (const std::string& method,
                            const Json::Value& params) override;

public:

  explicit CharonBackend (xaya::Game& game, PXLogic& rules)
    : rpc(game, rules, conn), srv(*this)
  {}

  void
  Start () override
  {
    LOG (INFO) << "Starting Charon server as " << FLAGS_charon_server_jid;
    srv.Connect (FLAGS_charon_server_jid, FLAGS_charon_password,
                 FLAGS_charon_priority);

    LOG (INFO) << "Using " << FLAGS_charon_pubsub_service << " for pubsub";
    srv.AddPubSub (FLAGS_charon_pubsub_service);

    AddNotification (std::make_unique<charon::StateChangeNotification> (),
                     &PXRpcServer::waitforchangeI);
    AddNotification (std::make_unique<charon::PendingChangeNotification> (),
                     &PXRpcServer::waitforpendingchangeI);
  }

  void
  Stop () override
  {
    LOG (INFO) << "Stopping Charon server...";
    srv.Disconnect ();
  }

};

Json::Value
CharonBackend::HandleMethod (const std::string& method,
                             const Json::Value& params)
{
  const auto mit = CHARON_METHODS.find (method);
  if (mit == CHARON_METHODS.end ())
    throw jsonrpc::JsonRpcException (
        jsonrpc::Errors::ERROR_RPC_METHOD_NOT_FOUND);

  Json::Value result;
  (rpc.*(mit->second)) (params, result);
  return result;
}

} // anonymous namespace

std::unique_ptr<xaya::GameComponent>
MaybeBuildCharonServer (xaya::Game& g, PXLogic& r)
{
  if (FLAGS_charon != "server")
    {
      LOG (INFO) << "Charon server is not enabled";
      return nullptr;
    }

  if (FLAGS_charon_server_jid.empty () || FLAGS_charon_password.empty ()
        || FLAGS_charon_pubsub_service.empty ())
    {
      LOG (ERROR)
          << "--charon_server_jid, --charon_password and"
             " --charon_pubsub_service must be given,"
             " Charon server will be disabled";
      return nullptr;
    }

  return std::make_unique<CharonBackend> (g, r);
}

} // namespace pxd
