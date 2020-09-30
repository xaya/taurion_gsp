// Copyright (C) 2020 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rest.hpp"

#include "gamestatejson.hpp"

#include <microhttpd.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>

namespace pxd
{

namespace
{

DEFINE_int32 (rest_bootstrap_refresh_seconds, 60 * 60,
              "the refresh interval for bootstrap data in seconds");

} // anonymous namespace

std::shared_ptr<RestApi::SuccessResult>
RestApi::ComputeBootstrapData ()
{
  const Json::Value val = logic.GetCustomStateData (game,
    [] (GameStateJson& gsj)
      {
        return gsj.BootstrapData ();
      });
  auto res = std::make_shared<SuccessResult> (SuccessResult (val).Gzip ());

  if (val["state"].asString () == "up-to-date")
    {
      LOG (INFO) << "Refreshing bootstrap-data cache";
      std::lock_guard<std::mutex> lock(mutBootstrap);
      bootstrapData = res;
    }
  else
    LOG (WARNING) << "We are still catching up, not caching bootstrap data";

  return res;
}

RestApi::SuccessResult
RestApi::Process (const std::string& url)
{
  std::string remainder;
  if (MatchEndpoint (url, "/bootstrap.json.gz", remainder) && remainder == "")
    {
      std::shared_ptr<SuccessResult> res;
      {
        std::lock_guard<std::mutex> lock(mutBootstrap);
        res = bootstrapData;
      }
      if (res == nullptr)
        res = ComputeBootstrapData ();
      CHECK (res != nullptr);
      return *res;
    }

  throw HttpError (MHD_HTTP_NOT_FOUND, "invalid API endpoint");
}

void
RestApi::Start ()
{
  xaya::RestApi::Start ();

  std::lock_guard<std::mutex> lock(mutStop);
  shouldStop = false;
  CHECK (bootstrapRefresher == nullptr);
  bootstrapRefresher = std::make_unique<std::thread> ([this] ()
    {
      const auto intv
          = std::chrono::seconds (FLAGS_rest_bootstrap_refresh_seconds);
      while (true)
        {
          ComputeBootstrapData ();

          std::unique_lock<std::mutex> lock(mutStop);
          if (shouldStop)
            break;
          cvStop.wait_for (lock, intv);
          if (shouldStop)
            break;
        }
    });
}

void
RestApi::Stop ()
{
  {
    std::lock_guard<std::mutex> lock(mutStop);
    shouldStop = true;
    cvStop.notify_all ();
  }

  if (bootstrapRefresher != nullptr)
    {
      bootstrapRefresher->join ();
      bootstrapRefresher.reset ();
    }

  xaya::RestApi::Stop ();
}

Json::Value
RestClient::GetBootstrapData ()
{
  Request req(*this);
  if (!req.Send ("/bootstrap.json.gz"))
    throw std::runtime_error (req.GetError ());

  if (req.GetType () != "application/json")
    throw std::runtime_error ("response is not JSON");

  return req.GetJson ();
}

} // namespace pxd
