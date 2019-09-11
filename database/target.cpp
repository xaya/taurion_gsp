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
  stmt.Bind (1, centre.GetX () - l1range);
  stmt.Bind (2, centre.GetX () + l1range);
  stmt.Bind (3, centre.GetY () - l1range);
  stmt.Bind (4, centre.GetY () + l1range);

  BindFactionParameter (stmt, 5, faction);

  class TargetsResult
  {};

  auto res = stmt.Query<TargetsResult> ();
  while (res.Step ())
    {
      const HexCoord coord (res.Get<int64_t> ("x"), res.Get<int64_t> ("y"));
      if (HexCoord::DistanceL1 (centre, coord) > l1range)
        continue;

      proto::TargetId targetId;
      targetId.set_id (res.Get<int64_t> ("id"));
      targetId.set_type (proto::TargetId::TYPE_CHARACTER);

      cb (coord, targetId);
    }
}

} // namespace pxd
