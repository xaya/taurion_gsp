#include "logic.hpp"

#include <glog/logging.h>

namespace pxd
{

void
PXLogic::SetupSchema (sqlite3* db)
{
  LOG (WARNING) << "No database schema set up";
}

void
PXLogic::GetInitialStateBlock (unsigned& height,
                               std::string& hashHex) const
{
  switch (GetChain ())
    {
    case xaya::Chain::MAIN:
      height = 430000;
      hashHex
          = "38aa107ef495c3878b2608ce951eb51ecd49ea4a5e9094201c12a8c0e2561e0c";
      break;

    case xaya::Chain::TEST:
      height = 10000;
      hashHex
          = "73d771be03c37872bc8ccd92b8acb8d7aa3ac0323195006fb3d3476784981a37";
      break;

    case xaya::Chain::REGTEST:
      height = 0;
      hashHex
          = "6f750b36d22f1dc3d0a6e483af45301022646dfc3b3ba2187865f5a7d6d83ab1";
      break;

    default:
      LOG (FATAL) << "Unexpected chain: " << xaya::ChainToString (GetChain ());
    }
}

void
PXLogic::InitialiseState (sqlite3* db)
{
  LOG (WARNING) << "Empty initial state for now";
}

void
PXLogic::UpdateState (sqlite3* db, const Json::Value& blockData)
{
  LOG (WARNING) << "Block data should not be ignored!";
}

Json::Value
PXLogic::GetStateAsJson (sqlite3* db)
{
  LOG (WARNING) << "Returning empty JSON as game state for now";
  return Json::Value (Json::objectValue);
}

} // namespace pxd
