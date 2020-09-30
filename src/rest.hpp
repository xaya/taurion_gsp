// Copyright (C) 2020 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PXD_REST_HPP
#define PXD_REST_HPP

#include "logic.hpp"

#include <xayagame/game.hpp>
#include <xayagame/rest.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

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

  /**
   * The current bootstrap data payload, if we have one cached.  This is a
   * shared pointer, so that we can create copies quickly (while holding the
   * lock) and then release it again while we send the result to a client.
   */
  std::shared_ptr<SuccessResult> bootstrapData;

  /** Lock for the bootstrap data cache.  */
  std::mutex mutBootstrap;

  /** Set to true if we should stop.  */
  bool shouldStop;

  /** Condition variable that is signaled if shouldStop is set.  */
  std::condition_variable cvStop;

  /** Mutex for the stop flag and condition variable.  */
  std::mutex mutStop;

  /** Thread running the bootstrap data update.  */
  std::unique_ptr<std::thread> bootstrapRefresher;

  /**
   * Computes the bootstrap data and returns it.  This may fill in the
   * cache (if we are up-to-date), but does not use an existing cache.
   */
  std::shared_ptr<SuccessResult> ComputeBootstrapData ();

protected:

  SuccessResult Process (const std::string& url) override;

public:

  explicit RestApi (xaya::Game& g, PXLogic& l, const int p)
    : xaya::RestApi(p), game(g), logic(l)
  {}

  void Start () override;
  void Stop () override;

};

/**
 * REST client for the Taurion API.
 */
class RestClient : public xaya::RestClient
{

public:

  using xaya::RestClient::RestClient;

  /**
   * Queries for the bootstrap data.  May throw a std::runtime_error
   * if the request fails.
   */
  Json::Value GetBootstrapData ();

};

} // namespace pxd

#endif // PXD_REST_HPP
