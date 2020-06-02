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

#include "prospecting.hpp"

#include "resourcedist.hpp"

#include "database/itemcounts.hpp"
#include "proto/roconfig.hpp"

namespace pxd
{

bool
CanProspectRegion (const Character& c, const Region& r, const Context& ctx)
{
  const auto& rpb = r.GetProto ();

  if (rpb.has_prospecting_character ())
    {
      LOG (WARNING)
          << "Region " << r.GetId ()
          << " is already being prospected by character "
          << rpb.prospecting_character ()
          << ", can't be prospected by " << c.GetId ();
      return false;
    }

  if (!rpb.has_prospection ())
    return true;

  if (ctx.Height () < rpb.prospection ().height ()
                        + ctx.Params ().ProspectionExpiryBlocks ())
    {
      LOG (WARNING)
          << "Height " << ctx.Height ()
          << " is too early to reprospect region " << r.GetId ()
          << " by " << c.GetId ()
          << "; the region was prospected last at height "
          << rpb.prospection ().height ();
      return false;
    }

  if (r.GetResourceLeft () > 0)
    {
      LOG (WARNING)
          << "Region " << r.GetId ()
          << " has " << r.GetResourceLeft ()
          << " of " << rpb.prospection ().resource ()
          << " left to be mined, can't be reprospected";
      return false;
    }

  return true;
}

void
FinishProspecting (Character& c, Database& db, RegionsTable& regions,
                   xaya::Random& rnd, const Context& ctx)
{
  const RoConfig cfg(ctx.Chain ());

  const auto& pos = c.GetPosition ();
  const auto regionId = ctx.Map ().Regions ().GetRegionId (pos);
  LOG (INFO)
      << "Character " << c.GetId ()
      << " finished prospecting region " << regionId;

  auto r = regions.GetById (regionId);
  auto& mpb = r->MutableProto ();
  CHECK_EQ (mpb.prospecting_character (), c.GetId ());
  mpb.clear_prospecting_character ();
  CHECK (!mpb.has_prospection ());
  auto* prosp = mpb.mutable_prospection ();
  prosp->set_name (c.GetOwner ());
  prosp->set_height (ctx.Height ());

  /* Determine the mine-able resource here.  */
  std::string type;
  Inventory::QuantityT amount;
  DetectResource (pos, cfg->resource_dist (), rnd, type, amount);
  prosp->set_resource (type);
  r->SetResourceLeft (amount);

  /* Check the prizes in order to see if we won any.  */
  const bool lowChance = ctx.Params ().IsLowPrizeZone (pos);
  ItemCounts cnt(db);
  for (const auto& p : ctx.Params ().ProspectingPrizes ())
    {
      const std::string prizeItem = p.name + " prize";
      const unsigned found = cnt.GetFound (prizeItem);
      CHECK_LE (found, p.number);
      if (found == p.number)
        continue;

      /* If we are in the "low prize" zone, reduce odds for finding the
         specific prize by 45% (to 55% of what they were).  */
      if (!rnd.ProbabilityRoll (lowChance ? 55 : 100, 100 * p.probability))
        continue;

      LOG (INFO)
        << "Character " << c.GetId ()
        << " found a prize of tier " << p.name
        << " prospecting region " << regionId;
      cnt.IncrementFound (prizeItem);
      c.GetInventory ().AddFungibleCount (prizeItem, 1);
      break;
    }
}

} // namespace pxd
