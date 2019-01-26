#include "target.hpp"

namespace pxd
{

void
TargetFinder::ProcessL1Targets (const HexCoord& centre,
                                const HexCoord::IntT l1range,
                                const Faction faction,
                                const ProcessingFcn& cb)
{

  auto stmt = db.Prepare (R"(
    SELECT `x`, `y`, `id` FROM `characters`
      WHERE (`x` BETWEEN ?1 AND ?2) AND (`y` BETWEEN ?3 AND ?4)
              AND `faction` != ?5
      ORDER BY `id`
  )");

  /* The query is actually about an L-infinity range, since that is easy
     to formulate in the database.  This certainly includes the L1 range.  */
  stmt.Bind<int> (1, centre.GetX () - l1range);
  stmt.Bind<int> (2, centre.GetX () + l1range);
  stmt.Bind<int> (3, centre.GetY () - l1range);
  stmt.Bind<int> (4, centre.GetY () + l1range);

  BindFactionParameter (stmt, 5, faction);

  auto res = stmt.Query ("targets");
  while (res.Step ())
    {
      const HexCoord coord (res.Get<int> ("x"), res.Get<int> ("y"));
      if (HexCoord::DistanceL1 (centre, coord) > l1range)
        continue;

      proto::TargetId targetId;
      targetId.set_id (res.Get<int> ("id"));
      targetId.set_type (proto::TargetId::TYPE_CHARACTER);

      cb (coord, targetId);
    }
}

} // namespace pxd
