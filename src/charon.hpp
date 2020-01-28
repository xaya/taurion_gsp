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

#ifndef PXD_CHARON_HPP
#define PXD_CHARON_HPP

#include "logic.hpp"

#include <charon/client.hpp>
#include <xayagame/defaultmain.hpp>
#include <xayagame/game.hpp>

#include <jsonrpccpp/server/abstractserverconnector.h>

#include <memory>

namespace pxd
{

/**
 * Abstract interface for a Charon client.  The actual implementation (which
 * holds a real Charon client and a local RPC server) is an implementation
 * detail.
 */
class CharonClient
{

protected:

  CharonClient () = default;

public:

  virtual ~CharonClient () = default;

  /**
   * Sets up the JSON-RPC connector for the local server.
   */
  virtual void SetupLocalRpc (jsonrpc::AbstractServerConnector& conn) = 0;

  /**
   * Starts the client and local server, returning only when the server
   * should be stopped.
   */
  virtual void Run () = 0;

};

/**
 * Checks if a Charon server should be run (according to the command-line flags)
 * and constructs one wrapped as a GameComponent.
 */
std::unique_ptr<xaya::GameComponent> MaybeBuildCharonServer (
    xaya::Game& g, PXLogic& r);

/**
 * Checks if this should run as Charon client.  If so, returns a new instance.
 */
std::unique_ptr<CharonClient> MaybeBuildCharonClient ();

} // namespace pxd

#endif // PXD_CHARON_HPP
