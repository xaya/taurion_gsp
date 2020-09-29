// Copyright (C) 2020 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rest.hpp"

#include "gamestatejson.hpp"

#include <microhttpd.h>

namespace pxd
{

RestApi::SuccessResult
RestApi::Process (const std::string& url)
{
  std::string remainder;
  if (MatchEndpoint (url, "/bootstrap.json.gz", remainder) && remainder == "")
    {
      const Json::Value res = logic.GetCustomStateData (game,
        [] (GameStateJson& gsj)
          {
            return gsj.BootstrapData ();
          });
      return SuccessResult (res).Gzip ();
    }

  throw HttpError (MHD_HTTP_NOT_FOUND, "invalid API endpoint");
}

} // namespace pxd
