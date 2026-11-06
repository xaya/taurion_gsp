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

#include "hexagonal/ring.hpp"

#include <algorithm>
#include <tuple>

namespace pxd
{

namespace
{

/**
 * Returns true if a target of the given faction matches the faction
 * conditions of a query.
 */
inline bool
MatchesFaction (const Faction target, const Faction faction,
                const bool enemies, const bool friendlies)
{
  /* Faction conditions for an invalid faction match nothing (as a comparison
     with NULL in SQL).  */
  if (faction == Faction::INVALID)
    return enemies && friendlies;

  return target == faction ? friendlies : enemies;
}

struct TargetResult : public ResultWithFaction
{
  RESULT_COLUMN (std::string, type, 1);
  RESULT_COLUMN (int64_t, id, 2);
  RESULT_COLUMN (int64_t, x, 3);
  RESULT_COLUMN (int64_t, y, 4);
};

} // anonymous namespace

TargetFinder::TargetFinder (Database& db, const PositionFilter& ignore)
{
  /* Characters inside buildings have no position and are never targets.
     Neither are buildings of the ancient faction.  */
  auto stmt = db.Prepare (R"(
    SELECT `x`, `y`, `id`, `faction`, 'character' AS `type`
      FROM `characters`
      WHERE `x` IS NOT NULL AND `y` IS NOT NULL
    UNION ALL
    SELECT `x`, `y`, `id`, `faction`, 'building' AS `type`
      FROM `buildings`
      WHERE `x` IS NOT NULL AND `y` IS NOT NULL AND `faction` != 4
  )");

  auto res = stmt.Query<TargetResult> ();
  while (res.Step ())
    {
      Target t;
      t.pos = HexCoord (res.Get<TargetResult::x> (),
                        res.Get<TargetResult::y> ());
      t.faction = GetFactionFromColumn (res);
      t.id = res.Get<TargetResult::id> ();

      const auto type = res.Get<TargetResult::type> ();
      if (type == "building")
        t.type = proto::TargetId::TYPE_BUILDING;
      else if (type == "character")
        t.type = proto::TargetId::TYPE_CHARACTER;
      else
        LOG (FATAL) << "Unexpected target type: " << type;

      CHECK (positions.emplace (PositionKey (t.type, t.id), t.pos).second);
      if (ignore != nullptr && ignore (t.pos))
        continue;

      const auto f = static_cast<unsigned> (t.faction);
      CHECK_LT (f, NUM_FACTIONS);
      auto& cell = cells[CoordKey (CellIndex (t.pos.GetX ()),
                                   CellIndex (t.pos.GetY ()))];
      cell.targets.push_back (t);
      ++cell.counts[f];
      tiles[CoordKey (t.pos.GetX (), t.pos.GetY ())].push_back (t);
    }
}

template <typename Fcn, typename Done>
  void
  TargetFinder::ForEachInRange (const HexCoord& centre,
                                const HexCoord::IntT l1range,
                                const Faction faction,
                                const bool enemies, const bool friendlies,
                                const Fcn& fcn, const Done& done) const
{
  CHECK (enemies || friendlies)
      << "Neither enemy nor friendly targets requested?";
  if (l1range < 0)
    return;

  /* Find the cells overlapping the L-infinity range (which certainly
     includes the L1 range) that have targets of matching factions, and
     count those targets.  */
  std::vector<const Cell*> overlapping;
  size_t candidates = 0;
  const int minX = CellIndex (centre.GetX () - l1range);
  const int maxX = CellIndex (centre.GetX () + l1range);
  const int minY = CellIndex (centre.GetY () - l1range);
  const int maxY = CellIndex (centre.GetY () + l1range);
  for (int x = minX; x <= maxX; ++x)
    for (int y = minY; y <= maxY; ++y)
      {
        const auto mit = cells.find (CoordKey (x, y));
        if (mit == cells.end ())
          continue;

        size_t matching = 0;
        for (unsigned f = 0; f < NUM_FACTIONS; ++f)
          if (MatchesFaction (static_cast<Faction> (f), faction,
                              enemies, friendlies))
            matching += mit->second.counts[f];

        if (matching > 0)
          {
            overlapping.push_back (&mit->second);
            candidates += matching;
          }
      }

  /* Then either look at all those candidates, or at the tiles in range
     ring by ring (in order of distance), whichever means looking at less.  */
  const size_t tilesInRange = 1 + 3 * static_cast<size_t> (l1range)
                                    * (l1range + 1);
  if (tilesInRange < candidates)
    {
      for (HexCoord::IntT r = 0; r <= l1range; ++r)
        {
          for (const auto& c : L1Ring (centre, r))
            {
              const auto mit = tiles.find (CoordKey (c.GetX (), c.GetY ()));
              if (mit == tiles.end ())
                continue;

              for (const auto& t : mit->second)
                if (MatchesFaction (t.faction, faction, enemies, friendlies))
                  fcn (t, r);
            }

          if (done ())
            return;
        }
      return;
    }

  for (const auto* cell : overlapping)
    for (const auto& t : cell->targets)
      {
        if (!MatchesFaction (t.faction, faction, enemies, friendlies))
          continue;
        const auto dist = HexCoord::DistanceL1 (centre, t.pos);
        if (dist <= l1range)
          fcn (t, dist);
      }
}

namespace
{

/**
 * Sorts targets in the order of TargetKey (the type enum values sort
 * buildings before characters) and returns their IDs.
 */
template <typename T>
  std::vector<proto::TargetId>
  SortedIds (std::vector<const T*>& targets)
{
  std::sort (targets.begin (), targets.end (),
             [] (const T* a, const T* b)
               {
                 return std::tie (a->type, a->id) < std::tie (b->type, b->id);
               });

  std::vector<proto::TargetId> res(targets.size ());
  for (size_t i = 0; i < targets.size (); ++i)
    {
      res[i].set_type (targets[i]->type);
      res[i].set_id (targets[i]->id);
    }

  return res;
}

} // anonymous namespace

void
TargetFinder::ProcessL1Targets (const HexCoord& centre,
                                const HexCoord::IntT l1range,
                                const Faction faction,
                                const bool enemies, const bool friendlies,
                                const ProcessingFcn& cb) const
{
  std::vector<const Target*> found;
  ForEachInRange (centre, l1range, faction, enemies, friendlies,
    [&found] (const Target& t, const HexCoord::IntT dist)
    {
      found.push_back (&t);
    },
    [] () { return false; });

  const auto ids = SortedIds (found);
  for (size_t i = 0; i < ids.size (); ++i)
    cb (found[i]->pos, ids[i]);
}

std::vector<proto::TargetId>
TargetFinder::FindClosest (const HexCoord& centre,
                           const HexCoord::IntT l1range,
                           const Faction faction,
                           const bool enemies, const bool friendlies,
                           const proto::TargetId& exclude) const
{
  std::vector<const Target*> closest;
  HexCoord::IntT closestDist = 0;
  ForEachInRange (centre, l1range, faction, enemies, friendlies,
    [&] (const Target& t, const HexCoord::IntT dist)
    {
      if (t.type == exclude.type () && t.id == exclude.id ())
        return;
      if (!closest.empty () && dist > closestDist)
        return;
      if (closest.empty () || dist < closestDist)
        {
          closest.clear ();
          closestDist = dist;
        }
      closest.push_back (&t);
    },
    [&closest] () { return !closest.empty (); });

  return SortedIds (closest);
}

const HexCoord&
TargetFinder::GetPosition (const proto::TargetId& id) const
{
  const auto mit = positions.find (PositionKey (id.type (), id.id ()));
  CHECK (mit != positions.end ())
      << "Not a potential target:\n" << id.DebugString ();
  return mit->second;
}

} // namespace pxd
