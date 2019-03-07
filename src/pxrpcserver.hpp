#ifndef PXD_PXRPCSERVER_HPP
#define PXD_PXRPCSERVER_HPP

#include "rpc-stubs/pxrpcserverstub.h"

#include "logic.hpp"

#include <xayagame/game.hpp>

#include <json/json.h>
#include <jsonrpccpp/server.h>

namespace pxd
{

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

public:

  explicit PXRpcServer (xaya::Game& g, PXLogic& l,
                        jsonrpc::AbstractServerConnector& conn)
    : PXRpcServerStub(conn), game(g), logic(l)
  {}

  virtual void stop () override;
  virtual Json::Value getcurrentstate () override;
  virtual Json::Value waitforchange () override;

  virtual Json::Value findpath (int l1range, const Json::Value& source,
                                const Json::Value& target, int wpdist) override;
  virtual Json::Value getregionat (const Json::Value& coord) override;

};

} // namespace pxd

#endif // PXD_PXRPCSERVER_HPP
