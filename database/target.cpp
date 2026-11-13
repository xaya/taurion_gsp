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
#include <limits>
#include <tuple>
#include <utility>

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

uint64_t
TargetFinder::Key (const proto::TargetId::Type type, const Database::IdT id)
{
  /* The type enum values sort buildings before characters.  */
  CHECK_LT (id, uint64_t{1} << ID_BITS);
  return (static_cast<uint64_t> (type) << ID_BITS) | id;
}

proto::TargetId
TargetFinder::IdFromKey (const uint64_t key)
{
  proto::TargetId res;
  res.set_type (static_cast<proto::TargetId::Type> (key >> ID_BITS));
  res.set_id (key & ((uint64_t{1} << ID_BITS) - 1));
  return res;
}

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

  std::vector<std::pair<Target, Faction>> all;
  auto res = stmt.Query<TargetResult> ();
  while (res.Step ())
    {
      proto::TargetId::Type type;
      const auto typeStr = res.Get<TargetResult::type> ();
      if (typeStr == "building")
        type = proto::TargetId::TYPE_BUILDING;
      else if (typeStr == "character")
        type = proto::TargetId::TYPE_CHARACTER;
      else
        LOG (FATAL) << "Unexpected target type: " << typeStr;

      Target t;
      t.pos = HexCoord (res.Get<TargetResult::x> (),
                        res.Get<TargetResult::y> ());
      t.key = Key (type, res.Get<TargetResult::id> ());
      all.emplace_back (t, GetFactionFromColumn (res));
    }

  /* The lists of targets are filled in key order.  */
  std::sort (all.begin (), all.end (),
             [] (const auto& a, const auto& b)
               {
                 return a.first.key < b.first.key;
               });

  for (const auto& entry : all)
    {
      const auto& t = entry.first;
      const bool ignored = ignore != nullptr && ignore (t.pos);
      CHECK (entries.emplace (t.key, Entry {t.pos, entry.second, ignored})
                .second);
      if (ignored)
        continue;

      const auto f = static_cast<unsigned> (entry.second);
      CHECK_LT (f, NUM_FACTIONS);
      cells[HexCoord (CellIndex (t.pos.GetX ()),
                      CellIndex (t.pos.GetY ()))][f].push_back (t);
      tiles[t.pos][f].push_back (t);
    }
}

template <typename Fcn>
  void
  TargetFinder::ForEachInRange (const HexCoord& centre,
                                const HexCoord::IntT l1range,
                                const Faction faction,
                                const bool enemies, const bool friendlies,
                                const Fcn& fcn) const
{
  CHECK (enemies || friendlies)
      << "Neither enemy nor friendly targets requested?";
  if (l1range < 0)
    return;

  std::array<bool, NUM_FACTIONS> matching;
  for (unsigned f = 0; f < NUM_FACTIONS; ++f)
    matching[f] = MatchesFaction (static_cast<Faction> (f), faction,
                                  enemies, friendlies);

  /* Find the cells overlapping the L-infinity range (which certainly
     includes the L1 range) with their lists of targets of matching factions,
     and count those targets.  */
  std::vector<const std::vector<Target>*> overlapping;
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

        for (unsigned f = 0; f < NUM_FACTIONS; ++f)
          if (matching[f] && !mit->second[f].empty ())
            {
              overlapping.push_back (&mit->second[f]);
              candidates += mit->second[f].size ();
            }
      }

  /* Then either look at the tiles in range ring by ring, or at all those
     candidates, whichever means looking at less.  */
  std::vector<Span> spans;
  const size_t tilesInRange = 1 + 3 * static_cast<size_t> (l1range)
                                    * (l1range + 1);
  if (tilesInRange < candidates)
    {
      for (HexCoord::IntT r = 0; r <= l1range; ++r)
        {
          spans.clear ();
          for (const auto& c : L1Ring (centre, r))
            {
              const auto mit = tiles.find (c);
              if (mit == tiles.end ())
                continue;

              for (unsigned f = 0; f < NUM_FACTIONS; ++f)
                {
                  const auto& list = mit->second[f];
                  if (matching[f] && !list.empty ())
                    spans.push_back ({list.data (),
                                      list.data () + list.size ()});
                }
            }

          if (!spans.empty () && fcn (spans))
            return;
        }
      return;
    }

  std::vector<std::pair<HexCoord::IntT, const Target*>> inRange;
  for (const auto* list : overlapping)
    for (const auto& t : *list)
      {
        const auto dist = HexCoord::DistanceL1 (centre, t.pos);
        if (dist <= l1range)
          inRange.emplace_back (dist, &t);
      }
  std::sort (inRange.begin (), inRange.end (),
             [] (const auto& a, const auto& b)
               {
                 return std::tie (a.first, a.second->key)
                          < std::tie (b.first, b.second->key);
               });

  for (auto it = inRange.begin (); it != inRange.end (); )
    {
      spans.clear ();
      const auto dist = it->first;
      for (; it != inRange.end () && it->first == dist; ++it)
        spans.push_back ({it->second, it->second + 1});

      if (fcn (spans))
        return;
    }
}

std::vector<proto::TargetId>
TargetFinder::FindInRange (const HexCoord& centre,
                           const HexCoord::IntT l1range,
                           const Faction faction,
                           const bool enemies, const bool friendlies,
                           const proto::TargetId& exclude,
                           const size_t limit) const
{
  const uint64_t excluded = Key (exclude.type (), exclude.id ());
  std::vector<uint64_t> found;
  ForEachInRange (centre, l1range, faction, enemies, friendlies,
    [&] (const std::vector<Span>& spans)
    {
      /* Of the targets at this distance, the first ones in key order are
         needed if there is a limit.  They are among the first of each list.  */
      const size_t need = (limit > 0 ? limit - found.size ()
                                     : std::numeric_limits<size_t>::max ());
      const size_t start = found.size ();
      for (const auto& s : spans)
        {
          size_t taken = 0;
          for (const Target* t = s.begin; t != s.end && taken < need; ++t)
            if (t->key != excluded)
              {
                found.push_back (t->key);
                ++taken;
              }
        }

      if (found.size () - start > need)
        {
          std::nth_element (found.begin () + start,
                            found.begin () + start + need, found.end ());
          found.resize (start + need);
        }

      return limit > 0 && found.size () == limit;
    });

  std::sort (found.begin (), found.end ());

  std::vector<proto::TargetId> res;
  res.reserve (found.size ());
  for (const auto key : found)
    res.push_back (IdFromKey (key));

  return res;
}

TargetFinder::Closest
TargetFinder::FindClosest (const HexCoord& centre,
                           const HexCoord::IntT l1range,
                           const Faction faction,
                           const bool enemies, const bool friendlies,
                           const proto::TargetId& exclude) const
{
  const uint64_t excluded = Key (exclude.type (), exclude.id ());
  Closest res;
  ForEachInRange (centre, l1range, faction, enemies, friendlies,
    [&] (const std::vector<Span>& spans)
    {
      res = Closest (spans, excluded);
      return !res.empty ();
    });

  return res;
}

bool
TargetFinder::IsInRange (const proto::TargetId& id, const HexCoord& centre,
                         const HexCoord::IntT l1range, const Faction faction,
                         const bool enemies, const bool friendlies) const
{
  const auto mit = entries.find (Key (id.type (), id.id ()));
  if (mit == entries.end ())
    return false;

  const auto& e = mit->second;
  return !e.ignored && HexCoord::DistanceL1 (centre, e.pos) <= l1range
            && MatchesFaction (e.faction, faction, enemies, friendlies);
}

const HexCoord*
TargetFinder::FindPosition (const proto::TargetId& id) const
{
  const auto mit = entries.find (Key (id.type (), id.id ()));
  return mit == entries.end () ? nullptr : &mit->second.pos;
}

TargetFinder::Closest::Closest (const std::vector<Span>& s,
                                const uint64_t exclude)
  : spans(s)
{
  for (const auto& span : spans)
    count += span.end - span.begin;

  excludedRank = CountUpTo (exclude - 1);
  if (CountUpTo (exclude) > excludedRank)
    --count;
  else
    excludedRank = count;
}

size_t
TargetFinder::Closest::CountUpTo (const uint64_t key) const
{
  size_t res = 0;
  for (const auto& s : spans)
    res += std::upper_bound (s.begin, s.end, key,
                             [] (const uint64_t k, const Target& t)
                               {
                                 return k < t.key;
                               })
              - s.begin;

  return res;
}

proto::TargetId
TargetFinder::Closest::operator[] (size_t i) const
{
  CHECK_LT (i, count);
  if (i >= excludedRank)
    ++i;

  if (spans.size () == 1)
    return IdFromKey (spans.front ().begin[i].key);

  /* Of the targets in all spans, the i-th in key order has the smallest key
     that more than i of them are up to.  */
  uint64_t lo = std::numeric_limits<uint64_t>::max ();
  uint64_t hi = 0;
  for (const auto& s : spans)
    {
      lo = std::min (lo, s.begin->key);
      hi = std::max (hi, (s.end - 1)->key);
    }
  while (lo < hi)
    {
      const uint64_t mid = lo + (hi - lo) / 2;
      if (CountUpTo (mid) > i)
        hi = mid;
      else
        lo = mid + 1;
    }

  return IdFromKey (lo);
}

} // namespace pxd
