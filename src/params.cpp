#include "params.hpp"

#include <glog/logging.h>

namespace pxd
{

std::string
Params::DeveloperAddress () const
{
  /* The address returned here is the premine address controlled by the Xaya
     team for the various chains.  See also Xaya Core's src/chainparams.cpp.  */

  switch (chain)
    {
    case xaya::Chain::MAIN:
      return "DHy2615XKevE23LVRVZVxGeqxadRGyiFW4";
    case xaya::Chain::TEST:
      return "dSFDAWxovUio63hgtfYd3nz3ir61sJRsXn";
    case xaya::Chain::REGTEST:
      return "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p";
    default:
      LOG (FATAL) << "Invalid chain value: " << static_cast<int> (chain);
    }
}

Amount
Params::CharacterCost () const
{
  return 5 * COIN;
}

} // namespace pxd
