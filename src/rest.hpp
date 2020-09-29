// Copyright (C) 2020 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PXD_REST_HPP
#define PXD_REST_HPP

#include "logic.hpp"

#include <xayagame/game.hpp>
#include <xayagame/rest.hpp>

namespace pxd
{

/**
 * HTTP server providing a REST API for tauriond.
 */
class RestApi : public xaya::RestApi
{

private:

  /** The underlying Game instance that manages everything.  */
  xaya::Game& game;

  /** The game logic implementation.  */
  PXLogic& logic;

protected:

  SuccessResult Process (const std::string& url) override;

public:

  explicit RestApi (xaya::Game& g, PXLogic& l, const int p)
    : xaya::RestApi(p), game(g), logic(l)
  {}

};

} // namespace pxd

#endif // PXD_REST_HPP
