/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#include "database/prizes.hpp"
#include "proto/roconfig.hpp"

namespace pxd
{

void
InitialisePrizes (Database& db, const Params& params)
{
  auto stmt = db.Prepare (R"(
    INSERT INTO `prizes` (`name`, `found`) VALUES (?1, 0)
  )");

  for (const auto& p : params.ProspectingPrizes ())
    {
      stmt.Reset ();
      stmt.Bind (1, p.name);
      stmt.Execute ();
    }
}

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
  const auto& pos = c.GetPosition ();
  const auto regionId = ctx.Map ().Regions ().GetRegionId (pos);
  LOG (INFO)
      << "Character " << c.GetId ()
      << " finished prospecting region " << regionId;

  CHECK_EQ (c.GetBusy (), 1);
  c.SetBusy (0);

  auto& cpb = c.MutableProto ();
  CHECK (cpb.has_prospection ());
  cpb.clear_prospection ();

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
  DetectResource (pos, RoConfigData ().resource_dist (), rnd, type, amount);
  prosp->set_resource (type);
  r->SetResourceLeft (amount);

  if (!ctx.Params ().CanWinPrizesAt (pos))
    {
      LOG (INFO) << "No prizes can be won at " << pos;
      return;
    }

  /* Check the prizes in order to see if we won any.  */
  Prizes prizeTable(db);
  for (const auto& p : ctx.Params ().ProspectingPrizes ())
    {
      const unsigned found = prizeTable.GetFound (p.name);
      CHECK_LE (found, p.number);
      if (found == p.number)
        continue;

      if (!rnd.ProbabilityRoll (1, p.probability))
        continue;

      LOG (INFO)
        << "Character " << c.GetId ()
        << " found a prize of tier " << p.name
        << " prospecting region " << regionId;
      prizeTable.IncrementFound (p.name);
      c.GetInventory ().AddFungibleCount (p.name + " prize", 1);
      break;
    }
}

} // namespace pxd
