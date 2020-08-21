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

#include "burnsale.hpp"

#include <glog/logging.h>

namespace pxd
{

Amount
ComputeBurnsaleAmount (Amount& burntChi, Amount alreadySold,
                       const Context& ctx)
{
  CHECK_GE (burntChi, 0);
  CHECK_GE (alreadySold, 0);

  Amount res = 0;
  for (const auto& stage : ctx.RoConfig ()->params ().burnsale_stages ())
    {
      /* Reduce the available amount in this stage by whatever was already
         sold previously (and has not yet been matched against previous
         stages in earlier iterations).  */
      Amount available = stage.amount_sold ();
      const Amount before = std::min (alreadySold, available);
      available -= before;
      alreadySold -= before;
      if (available == 0)
        continue;

      CHECK_GT (available, 0);
      CHECK_EQ (alreadySold, 0);

      /* Figure out how much of the remaining coins in this stage can
         be bought based on the price and available (burnt) CHI.  */
      const Amount affordable = burntChi / stage.price_sat ();
      const Amount sold = std::min (affordable, available);
      res += sold;
      burntChi -= sold * stage.price_sat ();

      /* If not all from this stage was bought, we are done.  Else continue
         trying future stages.  */
      if (sold < available)
        break;
      CHECK_EQ (sold, available);
    }

  return res;
}

} // namespace pxd
