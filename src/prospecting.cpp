#include "prospecting.hpp"

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
FinishProspecting (Character& c, RegionsTable& regions, const BaseMap& map)
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
  mpb.mutable_prospection ()->set_name (c.GetOwner ());
}

} // namespace pxd
