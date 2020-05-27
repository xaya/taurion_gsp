/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#include "mining.hpp"

#include "database/inventory.hpp"
#include "database/region.hpp"
#include "proto/roconfig.hpp"

namespace pxd
{

void
StopMining (Character& c)
{
  if (!c.GetProto ().has_mining ())
    return;

  VLOG_IF (1, c.GetProto ().mining ().active ())
      << "Stopping mining with character " << c.GetId ();

  c.MutableProto ().mutable_mining ()->clear_active ();
}

void
ProcessAllMining (Database& db, xaya::Random& rnd, const Context& ctx)
{
  CharacterTable characters(db);
  RegionsTable regions(db, ctx.Height ());

  auto res = characters.QueryMining ();
  while (res.Step ())
    {
      auto c = characters.GetFromResult (res);
      const auto pos = c->GetPosition ();
      const auto regionId = ctx.Map ().Regions ().GetRegionId (pos);
      VLOG (1)
          << "Processing mining of character " << c->GetId ()
          << " in region " << regionId << "...";
      auto r = regions.GetById (regionId);

      const auto& pb = c->GetProto ();
      CHECK (pb.has_mining ());

      /* It can happen that the region has (no longer) an active prospection
         entry.  For instance, if all resources were used up in the previous
         block and it is being re-prospected right away, then the previous
         prospection is cleared already when we process mining.  Thus, we need
         to handle this situation gracefully, and just stop mining.  */
      if (!r->GetProto ().has_prospection ())
        {
          LOG (WARNING)
              << "Region " << regionId
              << " is being mined by character " << c->GetId ()
              << " but is not prospected; stopping the mining operation";
          c->MutableProto ().mutable_mining ()->clear_active ();
          continue;
        }

      const auto& type = r->GetProto ().prospection ().resource ();
      const auto& rate = pb.mining ().rate ();
      Inventory::QuantityT mined
          = rate.min () + rnd.NextInt (rate.max () - rate.min () + 1);
      CHECK_GE (mined, 0);
      VLOG (1) << "Trying to mine " << mined << " of " << type;

      /* If we rolled to not mine anything, just continue processing the
         next character right away.  In this case, we do not want the
         "stop logic" below to execute at all.  */
      if (mined == 0)
        continue;

      /* Restrict the quantity by what is left in the region.  */
      Inventory::QuantityT left = r->GetResourceLeft ();
      CHECK_GE (left, 0);
      if (mined > left)
        {
          VLOG (1) << "Only " << left << " is left for mining";
          mined = left;
        }

      /* Restrict the quantity by cargo space.  */
      const int64_t freeCargo = pb.cargo_space () - c->UsedCargoSpace ();
      CHECK_GE (freeCargo, 0);

      const auto itemSpace = RoConfig ().Item (type).space ();
      CHECK_GT (itemSpace, 0)
          << "Minable resource " << type << " has zero space";

      const auto maxForSpace = freeCargo / itemSpace;
      if (mined > maxForSpace)
        {
          VLOG (1)
              << "Character " << c->GetId ()
              << " has only cargo space " << freeCargo
              << " left, which allows for " << maxForSpace << " of " << type;
          mined = maxForSpace;
        }

      /* Apply the updates.  */
      if (mined > 0)
        {
          left -= mined;
          r->SetResourceLeft (left);
          c->GetInventory ().AddFungibleCount (type, mined);
          VLOG (1)
              << "Mined " << mined << " of " << type
              << " with character " << c->GetId ();
        }
      else
        {
          VLOG (1)
              << "Character " << c->GetId ()
              << " cannot mine any more currently, stopping the operation";
          c->MutableProto ().mutable_mining ()->clear_active ();
        }
    }
}

} // namespace pxd
