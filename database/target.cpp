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
#include <utility>

namespace pxd
{

namespace
{

/** Side length of the square cells that targets are bucketed into.  */
constexpr int CELL_SIZE = 8;

/**
 * Returns the cell index (along one axis) of a coordinate.
 */
int
CellIndex (const int coord)
{
  return coord >= 0 ? coord / CELL_SIZE
                    : -((CELL_SIZE - 1 - coord) / CELL_SIZE);
}

/**
 * Returns the key of a target in the map of positions.
 */
uint64_t
PositionKey (const proto::TargetId::Type type, const Database::IdT id)
{
  return (id << 2) | type;
}

/**
 * Returns true if a target of the given faction matches the faction
 * conditions of a query.
 */
bool
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
      auto& cell = cells[HexCoord (CellIndex (t.pos.GetX ()),
                                   CellIndex (t.pos.GetY ()))];
      cell.targets.push_back (t);
      ++cell.counts[f];
      tiles[t.pos].push_back (t);
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
        const auto mit = cells.find (HexCoord (x, y));
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

  /* There are two ways to find the targets in range now:  Check the distance
     of all those candidates, or look up the targets on each tile in range,
     ring by ring (in order of distance).  We do whichever looks at less.
     The ring at distance d has 6 d tiles, which together with the centre
     makes 1 + 3 r (r + 1) tiles within range r.  */
  const size_t tilesInRange = 1 + 3 * static_cast<size_t> (l1range)
                                    * (l1range + 1);
  if (tilesInRange < candidates)
    {
      for (HexCoord::IntT r = 0; r <= l1range; ++r)
        {
          for (const auto& c : L1Ring (centre, r))
            {
              const auto mit = tiles.find (c);
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

std::vector<proto::TargetId>
TargetFinder::FindInRange (const HexCoord& centre,
                           const HexCoord::IntT l1range,
                           const Faction faction,
                           const bool enemies, const bool friendlies,
                           const proto::TargetId& exclude,
                           const size_t limit) const
{
  std::vector<std::pair<HexCoord::IntT, const Target*>> found;
  ForEachInRange (centre, l1range, faction, enemies, friendlies,
    [&] (const Target& t, const HexCoord::IntT dist)
    {
      if (t.type != exclude.type () || t.id != exclude.id ())
        found.emplace_back (dist, &t);
    },
    [&] () { return found.size () >= limit; });

  if (found.size () > limit)
    {
      const auto closer = [] (const auto& a, const auto& b)
        {
          return std::tie (a.first, a.second->type, a.second->id)
                    < std::tie (b.first, b.second->type, b.second->id);
        };
      std::nth_element (found.begin (), found.begin () + limit,
                        found.end (), closer);
      found.resize (limit);
    }

  std::vector<const Target*> targets;
  targets.reserve (found.size ());
  for (const auto& entry : found)
    targets.push_back (entry.second);

  return SortedIds (targets);
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

const HexCoord*
TargetFinder::FindPosition (const proto::TargetId& id) const
{
  const auto mit = positions.find (PositionKey (id.type (), id.id ()));
  return mit == positions.end () ? nullptr : &mit->second;
}

} // namespace pxd
