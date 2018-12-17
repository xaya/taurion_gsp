#include "pxrpcserver.hpp"

#include <xayagame/uint256.hpp>

#include <glog/logging.h>

namespace pxd
{

void
PXRpcServer::stop ()
{
  LOG (INFO) << "RPC method called: stop";
  game.RequestStop ();
}

Json::Value
PXRpcServer::getcurrentstate ()
{
  LOG (INFO) << "RPC method called: getcurrentstate";
  return game.GetCurrentJsonState ();
}

Json::Value
PXRpcServer::waitforchange ()
{
  LOG (INFO) << "RPC method called: waitforchange";

  xaya::uint256 block;
  game.WaitForChange (&block);

  /* If there is no best block so far, return JSON null.  */
  if (block.IsNull ())
    return Json::Value ();

  /* Otherwise, return the block hash.  */
  return block.ToHex ();
}

Json::Value
PXRpcServer::findpath (const int l1range, const Json::Value& source,
                       const Json::Value& target, const int wpdist)
{
  /* FIXME: Actually implement this.  */
  return Json::Value ();
}

} // namespace pxd
