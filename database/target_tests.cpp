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

#include "building.hpp"
#include "character.hpp"
#include "dbtest.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace pxd
{
namespace
{

class TargetFinderTests : public DBTestWithSchema
{

protected:

  /**
   * Vector into which the Find methods insert the found targets with their
   * positions.
   */
  std::vector<std::pair<HexCoord, proto::TargetId>> found;

  BuildingsTable buildings;
  CharacterTable characters;

  TargetFinderTests ()
    : buildings(db), characters(db)
  {}

  /**
   * Inserts a test building at the given centre and with the given faction.
   * Returns the ID.
   */
  Database::IdT
  InsertBuilding (const HexCoord& pos, const Faction faction)
  {
    auto h = buildings.CreateNew ("checkmark", "", faction);
    h->SetCentre (pos);

    return h->GetId ();
  }

  /**
   * Inserts a test character at the given position and with the given faction.
   * Returns the ID.
   */
  Database::IdT
  InsertCharacter (const HexCoord& pos, const Faction faction)
  {
    auto h = characters.CreateNew ("domob", faction);
    h->SetPosition (pos);

    return h->GetId ();
  }

  /**
   * Calls FindInRange for a red "attacker" (excluding nothing) and appends
   * the targets it returns to found.
   */
  void
  Find (const HexCoord& centre, const HexCoord::IntT l1range,
        const bool enemies, const bool friendlies)
  {
    const TargetFinder finder(db);
    for (const auto& id : finder.FindInRange (centre, l1range, Faction::RED,
                                              enemies, friendlies,
                                              proto::TargetId ()))
      found.emplace_back (*finder.FindPosition (id), id);
  }

  /**
   * Finds the targets for a red "attacker", looking only for enemies.
   */
  void
  ProcessEnemies (const HexCoord& centre, const HexCoord::IntT l1range)
  {
    Find (centre, l1range, true, false);
  }

  /**
   * Finds the targets for a red "attacker", looking only for friendlies.
   */
  void
  ProcessFriendlies (const HexCoord& centre, const HexCoord::IntT l1range)
  {
    Find (centre, l1range, false, true);
  }

  /**
   * Finds the targets for a red "attacker", looking for anyone
   * (friendly and enemies).
   */
  void
  ProcessEveryone (const HexCoord& centre, const HexCoord::IntT l1range)
  {
    Find (centre, l1range, true, true);
  }

};

TEST_F (TargetFinderTests, CharacterFactions)
{
  InsertCharacter (HexCoord (0, 0), Faction::RED);
  const auto idEnemy1 = InsertCharacter (HexCoord (1, 1), Faction::GREEN);
  InsertCharacter (HexCoord (-1, 1), Faction::RED);
  const auto idEnemy2 = InsertCharacter (HexCoord (0, 0), Faction::BLUE);

  ProcessEnemies (HexCoord (0, 0), 2);

  ASSERT_EQ (found.size (), 2);

  EXPECT_EQ (found[0].first, HexCoord (1, 1));
  EXPECT_EQ (found[0].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[0].second.id (), idEnemy1);

  EXPECT_EQ (found[1].first, HexCoord (0, 0));
  EXPECT_EQ (found[1].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[1].second.id (), idEnemy2);
}

TEST_F (TargetFinderTests, InBuilding)
{
  characters.CreateNew ("domob", Faction::GREEN)->SetBuildingId (100);
  ProcessEnemies (HexCoord (0, 0), 1);
  EXPECT_TRUE (found.empty ());
}

TEST_F (TargetFinderTests, FindPosition)
{
  const auto outside = InsertCharacter (HexCoord (12, -15), Faction::GREEN);
  const auto ignored = InsertCharacter (HexCoord (9, -15), Faction::GREEN);
  const auto inBuilding = InsertCharacter (HexCoord (10, -15), Faction::GREEN);
  characters.GetById (inBuilding)->SetBuildingId (42);

  const auto id = [] (const Database::IdT i)
    {
      proto::TargetId res;
      res.set_type (proto::TargetId::TYPE_CHARACTER);
      res.set_id (i);
      return res;
    };

  const TargetFinder finder(db, [] (const HexCoord& c)
    {
      return c == HexCoord (9, -15);
    });

  ASSERT_NE (finder.FindPosition (id (outside)), nullptr);
  EXPECT_EQ (*finder.FindPosition (id (outside)), HexCoord (12, -15));
  /* Ignored targets (like those in no-combat zones) are only left out of
     the queries.  */
  ASSERT_NE (finder.FindPosition (id (ignored)), nullptr);
  EXPECT_EQ (*finder.FindPosition (id (ignored)), HexCoord (9, -15));
  EXPECT_EQ (finder.FindPosition (id (inBuilding)), nullptr);
  EXPECT_EQ (finder.FindPosition (id (12345)), nullptr);
}

TEST_F (TargetFinderTests, CharacterRange)
{
  const HexCoord centre(10, -15);
  const HexCoord::IntT range = 5;

  std::vector<std::pair<Database::IdT, HexCoord>> expected;
  for (HexCoord::IntT x = centre.GetX () - 2 * range;
       x <= centre.GetX () + 2 * range; ++x)
    for (HexCoord::IntT y = centre.GetY () - 2 * range;
         y <= centre.GetY () + 2 * range; ++y)
      {
        const HexCoord pos(x, y);
        const auto id = InsertCharacter (pos, Faction::GREEN);

        if (HexCoord::DistanceL1 (pos, centre) <= range)
          expected.emplace_back (id, pos);
      }

  ProcessEnemies (centre, range);

  ASSERT_EQ (found.size (), expected.size ());
  for (unsigned i = 0; i < expected.size (); ++i)
    {
      EXPECT_EQ (found[i].first, expected[i].second);
      EXPECT_EQ (found[i].second.type (), proto::TargetId::TYPE_CHARACTER);
      EXPECT_EQ (found[i].second.id (), expected[i].first);
    }
}

TEST_F (TargetFinderTests, BuildingFactions)
{
  const HexCoord pos(10, -15);

  InsertBuilding (pos, Faction::ANCIENT);
  InsertBuilding (pos, Faction::RED);
  const auto idEnemy1 = InsertBuilding (pos, Faction::GREEN);
  const auto idEnemy2 = InsertBuilding (pos, Faction::BLUE);

  ProcessEnemies (pos, 1);

  ASSERT_EQ (found.size (), 2);

  EXPECT_EQ (found[0].first, pos);
  EXPECT_EQ (found[0].second.type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (found[0].second.id (), idEnemy1);

  EXPECT_EQ (found[1].first, pos);
  EXPECT_EQ (found[1].second.type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (found[1].second.id (), idEnemy2);
}

TEST_F (TargetFinderTests, BuildingsAndCharacters)
{
  const HexCoord pos(10, -15);

  const auto building1 = InsertBuilding (pos, Faction::GREEN);
  const auto char1 = InsertCharacter (pos, Faction::BLUE);
  const auto building2 = InsertBuilding (pos, Faction::GREEN);
  const auto char2 = InsertCharacter (pos, Faction::BLUE);

  ProcessEnemies (pos, 1);

  ASSERT_EQ (found.size (), 4);

  EXPECT_EQ (found[0].second.type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (found[0].second.id (), building1);
  EXPECT_EQ (found[1].second.type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (found[1].second.id (), building2);

  EXPECT_EQ (found[2].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[2].second.id (), char1);
  EXPECT_EQ (found[3].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[3].second.id (), char2);
}

TEST_F (TargetFinderTests, Friendlies)
{
  const auto idCharacter = InsertCharacter (HexCoord (0, 0), Faction::RED);
  InsertCharacter (HexCoord (0, 0), Faction::GREEN);

  const auto idBuilding = InsertBuilding (HexCoord (0, 0), Faction::RED);
  InsertBuilding (HexCoord (0, 0), Faction::GREEN);
  InsertBuilding (HexCoord (0, 0), Faction::ANCIENT);

  ProcessFriendlies (HexCoord (0, 0), 2);

  ASSERT_EQ (found.size (), 2);

  EXPECT_EQ (found[0].second.type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (found[0].second.id (), idBuilding);

  EXPECT_EQ (found[1].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[1].second.id (), idCharacter);
}

TEST_F (TargetFinderTests, FriendlyAndEnemies)
{
  const auto idCharacter1 = InsertCharacter (HexCoord (0, 0), Faction::RED);
  const auto idCharacter2 = InsertCharacter (HexCoord (0, 0), Faction::GREEN);

  const auto idBuilding1 = InsertBuilding (HexCoord (0, 0), Faction::RED);
  const auto idBuilding2 = InsertBuilding (HexCoord (0, 0), Faction::GREEN);
  InsertBuilding (HexCoord (0, 0), Faction::ANCIENT);

  /* This should return all except for the ancient (neutral) building.  */
  ProcessEveryone (HexCoord (0, 0), 2);

  ASSERT_EQ (found.size (), 4);

  EXPECT_EQ (found[0].second.type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (found[0].second.id (), idBuilding1);
  EXPECT_EQ (found[1].second.type (), proto::TargetId::TYPE_BUILDING);
  EXPECT_EQ (found[1].second.id (), idBuilding2);

  EXPECT_EQ (found[2].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[2].second.id (), idCharacter1);
  EXPECT_EQ (found[3].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[3].second.id (), idCharacter2);
}

TEST_F (TargetFinderTests, Limit)
{
  const HexCoord centre(10, -15);

  /* At distance 0 (one of them excluded), 1 and 2.  Of those at distance 1
     the building and the lower-ID character come first in target order.  */
  const auto excluded = InsertCharacter (centre, Faction::GREEN);
  const auto atCentre = InsertCharacter (centre, Faction::GREEN);
  const auto far = InsertCharacter (HexCoord (12, -15), Faction::GREEN);
  const auto nearChar1 = InsertCharacter (HexCoord (11, -15), Faction::GREEN);
  const auto nearChar2 = InsertCharacter (HexCoord (10, -14), Faction::GREEN);
  const auto nearBuilding = InsertBuilding (HexCoord (9, -15), Faction::GREEN);

  proto::TargetId exclude;
  exclude.set_type (proto::TargetId::TYPE_CHARACTER);
  exclude.set_id (excluded);

  const auto ids = [&] (const size_t limit)
    {
      std::vector<std::pair<proto::TargetId::Type, Database::IdT>> res;
      for (const auto& id : TargetFinder (db).FindInRange (
                                centre, 5, Faction::RED, true, false,
                                exclude, limit))
        res.emplace_back (id.type (), id.id ());
      return res;
    };

  using V = std::vector<std::pair<proto::TargetId::Type, Database::IdT>>;
  constexpr auto B = proto::TargetId::TYPE_BUILDING;
  constexpr auto C = proto::TargetId::TYPE_CHARACTER;

  EXPECT_EQ (ids (1), V ({{C, atCentre}}));
  EXPECT_EQ (ids (2), V ({{B, nearBuilding}, {C, atCentre}}));
  EXPECT_EQ (ids (3), V ({{B, nearBuilding}, {C, atCentre}, {C, nearChar1}}));
  EXPECT_EQ (ids (4), V ({{B, nearBuilding}, {C, atCentre},
                          {C, nearChar1}, {C, nearChar2}}));

  const V all({{B, nearBuilding}, {C, atCentre}, {C, far},
               {C, nearChar1}, {C, nearChar2}});
  EXPECT_EQ (ids (5), all);
  EXPECT_EQ (ids (6), all);
  EXPECT_EQ (ids (0), all);
}

/**
 * The query that TargetFinder answered directly from the database before it
 * read all targets into memory, as the reference for its results.
 */
std::vector<std::pair<HexCoord, proto::TargetId>>
QueryDatabaseTargets (Database& db, const HexCoord& centre,
                      const HexCoord::IntT l1range, const Faction faction,
                      const bool enemies, const bool friendlies)
{
  std::string characterCond = "TRUE";
  std::string buildingCond = "`faction` != 4";
  if (!friendlies)
    {
      characterCond += " AND `faction` != ?5";
      buildingCond += " AND `faction` != ?5";
    }
  if (!enemies)
    {
      characterCond += " AND `faction` = ?5";
      buildingCond += " AND `faction` = ?5";
    }

  auto stmt = (*db).Prepare (R"(
    SELECT `x`, `y`, `id`, 'character' AS `type`
      FROM `characters`
      WHERE (`x` BETWEEN ?1 AND ?2) AND (`y` BETWEEN ?3 AND ?4) AND
  )" + characterCond + R"(
    UNION ALL
    SELECT `x`, `y`, `id`, 'building' AS `type`
      FROM `buildings`
      WHERE (`x` BETWEEN ?1 AND ?2) AND (`y` BETWEEN ?3 AND ?4) AND
  )" + buildingCond + R"(
    ORDER BY `type`, `id`
  )");
  stmt.Bind (1, centre.GetX () - l1range);
  stmt.Bind (2, centre.GetX () + l1range);
  stmt.Bind (3, centre.GetY () - l1range);
  stmt.Bind (4, centre.GetY () + l1range);
  if (!friendlies || !enemies)
    stmt.Bind (5, static_cast<int64_t> (faction));

  std::vector<std::pair<HexCoord, proto::TargetId>> res;
  while (stmt.Step ())
    {
      const HexCoord c(stmt.Get<int64_t> (0), stmt.Get<int64_t> (1));
      if (HexCoord::DistanceL1 (centre, c) > l1range)
        continue;

      proto::TargetId id;
      id.set_id (stmt.Get<int64_t> (2));
      id.set_type (stmt.Get<std::string> (3) == "building"
                      ? proto::TargetId::TYPE_BUILDING
                      : proto::TargetId::TYPE_CHARACTER);
      res.emplace_back (c, id);
    }

  return res;
}

TEST_F (TargetFinderTests, MatchesDatabaseQuery)
{
  /* Random targets (some inside buildings) across several cells, including
     negative coordinates, and random queries compared to the database.  */
  std::mt19937 rnd(42);
  const auto coord = [&rnd] ()
    {
      return HexCoord (std::uniform_int_distribution<int> (-40, 40) (rnd),
                       std::uniform_int_distribution<int> (-40, 40) (rnd));
    };
  const Faction factions[] = {Faction::RED, Faction::GREEN, Faction::BLUE};
  const auto faction = [&] ()
    {
      return factions[std::uniform_int_distribution<int> (0, 2) (rnd)];
    };

  std::vector<Database::IdT> buildingIds;
  for (unsigned i = 0; i < 100; ++i)
    buildingIds.push_back (InsertBuilding (
        coord (), i % 10 == 0 ? Faction::ANCIENT : faction ()));
  for (unsigned i = 0; i < 2'000; ++i)
    {
      const auto id = InsertCharacter (coord (), faction ());
      if (i % 20 == 0)
        characters.GetById (id)->SetBuildingId (buildingIds[i % 100]);
    }

  /* The second finder ignores some positions (like no-combat zones).  */
  const auto ignored = [] (const HexCoord& c)
    {
      return (c.GetX () + 100) % 7 == 0;
    };
  const TargetFinder finder(db);
  const TargetFinder filtered(db, ignored);

  for (unsigned i = 0; i < 1'000; ++i)
    {
      const HexCoord centre = coord ();
      /* Small and large ranges, so that queries are answered both by looking
         at the tiles in range and at the candidates in overlapping cells.  */
      const int maxRange = (i % 5 == 0 ? 40 : 6);
      const auto range
          = std::uniform_int_distribution<int> (0, maxRange) (rnd);
      const Faction f = faction ();
      const bool enemies = (i % 3 != 1);
      const bool friendlies = (i % 3 != 0);
      const bool filter = (i % 2 == 0);

      auto expected = QueryDatabaseTargets (db, centre, range, f,
                                            enemies, friendlies);
      if (filter)
        expected.erase (std::remove_if (expected.begin (), expected.end (),
                                        [&] (const auto& e)
                                          {
                                            return ignored (e.first);
                                          }),
                        expected.end ());

      const auto& query = (filter ? filtered : finder);
      const auto all = query.FindInRange (centre, range, f, enemies,
                                          friendlies, proto::TargetId ());
      ASSERT_EQ (all.size (), expected.size ());
      for (unsigned j = 0; j < all.size (); ++j)
        {
          EXPECT_EQ (*query.FindPosition (all[j]), expected[j].first);
          EXPECT_EQ (all[j].type (), expected[j].second.type ());
          EXPECT_EQ (all[j].id (), expected[j].second.id ());
        }

      /* Excluding one of them (if any), and with a limit:  the ones
         closest to the centre, of those at the same distance the first,
         and returned in target order.  */
      proto::TargetId exclude;
      if (!expected.empty ())
        exclude = expected[i % expected.size ()].second;
      const auto isExcluded = [&exclude] (const proto::TargetId& id)
        {
          return id.type () == exclude.type () && id.id () == exclude.id ();
        };

      const size_t limits[] = {1, 2, 5, 20};
      const size_t limit = limits[i % 4];
      std::vector<std::pair<HexCoord::IntT, size_t>> byDistance;
      for (size_t j = 0; j < expected.size (); ++j)
        if (!isExcluded (expected[j].second))
          byDistance.emplace_back (
              HexCoord::DistanceL1 (centre, expected[j].first), j);
      std::sort (byDistance.begin (), byDistance.end ());
      if (byDistance.size () > limit)
        byDistance.resize (limit);
      std::vector<size_t> limited;
      for (const auto& entry : byDistance)
        limited.push_back (entry.second);
      std::sort (limited.begin (), limited.end ());

      const auto actualLimited = query.FindInRange (
          centre, range, f, enemies, friendlies, exclude, limit);
      ASSERT_EQ (actualLimited.size (), limited.size ());
      for (unsigned j = 0; j < limited.size (); ++j)
        {
          EXPECT_EQ (actualLimited[j].type (),
                     expected[limited[j]].second.type ());
          EXPECT_EQ (actualLimited[j].id (), expected[limited[j]].second.id ());
        }

      /* The closest ones, with the same exclusion.  */
      std::vector<proto::TargetId> closest;
      HexCoord::IntT closestDist = range + 1;
      for (const auto& e : expected)
        {
          if (isExcluded (e.second))
            continue;
          const auto dist = HexCoord::DistanceL1 (centre, e.first);
          if (dist < closestDist)
            {
              closest.clear ();
              closestDist = dist;
            }
          if (dist == closestDist)
            closest.push_back (e.second);
        }

      const auto actual = query.FindClosest (
          centre, range, f, enemies, friendlies, exclude);
      ASSERT_EQ (actual.size (), closest.size ());
      for (unsigned j = 0; j < actual.size (); ++j)
        {
          EXPECT_EQ (actual[j].type (), closest[j].type ());
          EXPECT_EQ (actual[j].id (), closest[j].id ());
        }
    }
}

} // anonymous namespace
} // namespace pxd
