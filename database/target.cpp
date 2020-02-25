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

#include "target.hpp"

namespace pxd
{

namespace
{

struct TargetResult : public Database::ResultType
{
  RESULT_COLUMN (std::string, type, 1);
  RESULT_COLUMN (int64_t, id, 2);
  RESULT_COLUMN (int64_t, x, 3);
  RESULT_COLUMN (int64_t, y, 4);
};

} // anonymous namespace

void
TargetFinder::ProcessL1Targets (const HexCoord& centre,
                                const HexCoord::IntT l1range,
                                const Faction faction,
                                const ProcessingFcn& cb)
{
  /* Note that the "between" statement is automatically false for NULL values,
     hence characters in buildings are ignored (as they should).  */
  auto stmt = db.Prepare (R"(
    SELECT `x`, `y`, `id`, 'character' AS `type`
      FROM `characters`
      WHERE (`x` BETWEEN ?1 AND ?2) AND (`y` BETWEEN ?3 AND ?4)
              AND `faction` != ?5
    UNION ALL
    SELECT `x`, `y`, `id`, 'building' AS `type`
      FROM `buildings`
      WHERE (`x` BETWEEN ?1 AND ?2) AND (`y` BETWEEN ?3 AND ?4)
              AND `faction` != ?5 AND `faction` != 4
    ORDER BY `type`, `id`
  )");

  /* The query is actually about an L-infinity range, since that is easy
     to formulate in the database.  This certainly includes the L1 range.  */
  stmt.Bind (1, centre.GetX () - l1range);
  stmt.Bind (2, centre.GetX () + l1range);
  stmt.Bind (3, centre.GetY () - l1range);
  stmt.Bind (4, centre.GetY () + l1range);

  BindFactionParameter (stmt, 5, faction);

  auto res = stmt.Query<TargetResult> ();
  while (res.Step ())
    {
      const HexCoord coord (res.Get<TargetResult::x> (),
                            res.Get<TargetResult::y> ());
      if (HexCoord::DistanceL1 (centre, coord) > l1range)
        continue;

      proto::TargetId targetId;
      targetId.set_id (res.Get<TargetResult::id> ());

      const auto type = res.Get<TargetResult::type> ();
      if (type == "building")
        targetId.set_type (proto::TargetId::TYPE_BUILDING);
      else if (type == "character")
        targetId.set_type (proto::TargetId::TYPE_CHARACTER);
      else
        LOG (FATAL) << "Unexpected target type: " << type;

      cb (coord, targetId);
    }
}

} // namespace pxd
