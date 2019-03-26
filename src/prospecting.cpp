#include "prospecting.hpp"

#include "database/prizes.hpp"

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

void
FinishProspecting (Character& c, Database& db, RegionsTable& regions,
                   xaya::Random& rnd,
                   const Params& params, const BaseMap& map)
{
  const auto& pos = c.GetPosition ();
  const auto regionId = map.Regions ().GetRegionId (pos);
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

  /* Check the prizes in order to see if we won any.  */
  CHECK (!prosp->has_prize ());
  Prizes prizeTable(db);
  for (const auto& p : params.ProspectingPrizes ())
    {
      const unsigned found = prizeTable.GetFound (p.name);
      CHECK_LE (found, p.number);
      if (found == p.number)
        continue;

      const auto pick = rnd.NextInt (p.probability);
      if (pick != 0)
        continue;

      LOG (INFO)
        << "Character " << c.GetId ()
        << " found a prize of tier " << p.name
        << " prospecting region " << regionId;
      prizeTable.IncrementFound (p.name);
      prosp->set_prize (p.name);
      break;
    }
}

} // namespace pxd
