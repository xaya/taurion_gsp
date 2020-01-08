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

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>

namespace pxd
{

namespace
{

DEFINE_string (charon, "",
               "Whether to run a Charon server (\"server\"),"
               " client (\"client\") or nothing (default)");

DEFINE_string (charon_server_jid, "", "Bare or full JID for the Charon server");
DEFINE_string (charon_client_jid, "", "Bare or full JID for the Charon client");
DEFINE_string (charon_password, "", "XMPP password for the Charon JID");
DEFINE_int32 (charon_priority, 0, "Priority for the XMPP connection");

DEFINE_string (charon_pubsub_service, "",
               "The pubsub service to use on the Charon server");

/* ************************************************************************** */

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

/* ************************************************************************** */

/**
 * The actual CharonClient implementation.
 */
class RealCharonClient : public CharonClient
{

private:

  /**
   * Local RPC server that handles requests for the Charon client.
   */
  class RpcServer : public jsonrpc::AbstractServer<RpcServer>
  {

  private:

    /** Function pointer to a call on nonstate RPC.  */
    using NonStateMethod
        = void (NonStateRpcServer::*) (const Json::Value&, Json::Value&);

    /** Methods to forward to the nonstate RPC server.  */
    static const std::map<std::string, NonStateMethod> NONSTATE_METHODS;

    /**
     * Notification methods enabled on the client.  The value of each entry
     * is the type string we use on the Charon client.
     */
    std::map<std::string, std::string> notifications;

    /** RealCharonClient instance this is associated to.  */
    RealCharonClient& parent;

    /** BaseMap for the nonstate server.  */
    const BaseMap map;

    /** Null server connector for the nonstate instance.  */
    NullServerConnector nullConnector;

    /** NonStateRpcServer used to answer calls it supports.  */
    NonStateRpcServer nonstate;

    /**
     * Adds a method to the table of supported methods.
     */
    void
    AddMethod (const std::string& method)
    {
      jsonrpc::Procedure proc(method, jsonrpc::PARAMS_BY_POSITION,
                              jsonrpc::JSON_OBJECT, nullptr);
      bindAndAddMethod (proc, &RpcServer::neverCalled);
    }

    /**
     * Enables a new notification with the given type and method name
     * on the server.
     */
    template <typename Notification>
      void
      AddNotification (const std::string& method)
    {
      auto n = std::make_unique<Notification> ();
      const auto res = notifications.emplace (method, n->GetType ());
      CHECK (res.second) << "Duplicate notification: " << method;
      AddMethod (method);
      parent.client.AddNotification (std::move (n));
    }

    /**
     * Handler method for the stop notification.
     */
    void
    stop (const Json::Value& params)
    {
      std::lock_guard<std::mutex> lock(parent.mut);
      parent.shouldStop = true;
      parent.cv.notify_all ();
    }

    /**
     * Dummy handler method for all methods.  It will never be called
     * since those calls are intercepted in HandleMethodCall anyway.  We just
     * need something to pass for bindAndAddMethod.
     */
    void
    neverCalled (const Json::Value& params, Json::Value& result)
    {
      LOG (FATAL) << "method call not intercepted";
    }

  public:

    explicit RpcServer (RealCharonClient& p,
                        jsonrpc::AbstractServerConnector& conn);

    ~RpcServer ()
    {
      StopListening ();
    }

    void HandleMethodCall (jsonrpc::Procedure& proc, const Json::Value& params,
                           Json::Value& result) override;

  };

  /** The Charon client.  */
  charon::Client client;

  /** The RPC server, if one has been started / set up.  */
  std::unique_ptr<RpcServer> rpc;

  /** Mutex for stopping.  */
  std::mutex mut;

  /** Condition variable to wake up the main thread when stopped.  */
  std::condition_variable cv;

  /** Set to true when we should stop running.  */
  bool shouldStop;

public:

  explicit RealCharonClient (const std::string& serverJid)
    : client(serverJid)
  {
    LOG (INFO) << "Using " << serverJid << " as Charon server";
  }

  void SetupLocalRpc (jsonrpc::AbstractServerConnector& conn) override;
  void Run () override;

};

const std::map<std::string, RealCharonClient::RpcServer::NonStateMethod>
    RealCharonClient::RpcServer::NONSTATE_METHODS =
  {
    {"findpath", &NonStateRpcServer::findpathI},
    {"getregionat", &NonStateRpcServer::getregionatI},
  };

RealCharonClient::RpcServer::RpcServer (RealCharonClient& p,
                                        jsonrpc::AbstractServerConnector& conn)
  : jsonrpc::AbstractServer<RpcServer> (conn, jsonrpc::JSONRPC_SERVER_V2),
    parent(p), nonstate(nullConnector, map)
{
  jsonrpc::Procedure stopProc("stop", jsonrpc::PARAMS_BY_POSITION, nullptr);
  bindAndAddNotification (stopProc, &RpcServer::stop);

  for (const auto& entry : CHARON_METHODS)
    AddMethod (entry.first);
  for (const auto& entry : NONSTATE_METHODS)
    AddMethod (entry.first);

  AddNotification<charon::StateChangeNotification> ("waitforchange");
  AddNotification<charon::PendingChangeNotification> ("waitforpendingchange");
}

void
RealCharonClient::RpcServer::HandleMethodCall (jsonrpc::Procedure& proc,
                                               const Json::Value& params,
                                               Json::Value& result)
{
  const auto& method = proc.GetProcedureName ();

  if (CHARON_METHODS.find (method) != CHARON_METHODS.end ())
    {
      VLOG (1) << "Forwarding method " << method << " through Charon";
      result = parent.client.ForwardMethod (method, params);
      return;
    }

  const auto mitNonState = NONSTATE_METHODS.find (method);
  if (mitNonState != NONSTATE_METHODS.end ())
    {
      VLOG (1) << "Answering method " << method << " locally";
      (nonstate.*(mitNonState->second)) (params, result);
      return;
    }

  const auto mitWait = notifications.find (method);
  if (mitWait != notifications.end ())
    {
      VLOG (1) << "Notification waiter called: " << method;

      if (!params.isArray () || params.size () != 1)
        throw jsonrpc::JsonRpcException (
            jsonrpc::Errors::ERROR_RPC_INVALID_PARAMS,
            "wait method expects a single positional argument");

      result = parent.client.WaitForChange (mitWait->second, params[0]);
      return;
    }

  /* Since the upstream class dispatches methods, we should only ever arrive
     here with a method we added before.  */
  LOG (FATAL) << "Unknown method: " << method;
}

void
RealCharonClient::SetupLocalRpc (jsonrpc::AbstractServerConnector& conn)
{
  CHECK (rpc == nullptr);
  rpc = std::make_unique<RpcServer> (*this, conn);
}

void
RealCharonClient::Run ()
{
  LOG (INFO) << "Connecting client to XMPP as " << FLAGS_charon_client_jid;
  client.Connect (FLAGS_charon_client_jid, FLAGS_charon_password, -1);

  const std::string srvResource = client.GetServerResource ();
  if (srvResource.empty ())
    LOG (WARNING) << "Could not detect server";
  else
    LOG (INFO) << "Using server resource: " << srvResource;

  shouldStop = false;
  if (rpc != nullptr)
    rpc->StartListening ();

  {
    std::unique_lock<std::mutex> lock(mut);
    while (!shouldStop)
      cv.wait (lock);
  }

  if (rpc != nullptr)
    rpc->StopListening ();
  client.Disconnect ();
}

/* ************************************************************************** */

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

std::unique_ptr<CharonClient>
MaybeBuildCharonClient ()
{
  if (FLAGS_charon != "client")
    {
      LOG (INFO) << "Charon client is not enabled";
      return nullptr;
    }

  if (FLAGS_charon_server_jid.empty () || FLAGS_charon_client_jid.empty ()
        || FLAGS_charon_password.empty ())
    {
      LOG (ERROR)
          << "--charon_server_jid, --charon_client_jid and --charon_password"
             " must be given for Charon client mode";
      return nullptr;
    }

  return std::make_unique<RealCharonClient> (FLAGS_charon_server_jid);
}

} // namespace pxd
