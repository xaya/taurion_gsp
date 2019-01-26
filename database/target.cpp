#include "target.hpp"

#include "rangequery.hpp"

namespace pxd
{

void
TargetFinder::ProcessL1Targets (const HexCoord& centre,
                                const HexCoord::IntT l1range,
                                const Faction faction,
                                const ProcessingFcn& cb)
{
  const L1RangeQuery rq(db, centre, l1range);

  auto stmt = db.Prepare (R"(
    SELECT `x`, `y`, `id` FROM `characters`
  )" + rq.GetJoinClause () + R"(
      WHERE `faction` != ?1
      ORDER BY `id`
  )");
  BindFactionParameter (stmt, 1, faction);

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
