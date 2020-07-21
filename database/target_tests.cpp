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

#include <vector>

namespace pxd
{
namespace
{

class TargetFinderTests : public DBTestWithSchema
{

private:

  TargetFinder finder;

  /** Callback that inserts targets into found.  */
  TargetFinder::ProcessingFcn cb;

protected:

  /**
   * Vector into which found targets will be inserted by the constructed
   * callback function cb.
   */
  std::vector<std::pair<HexCoord, proto::TargetId>> found;

  BuildingsTable buildings;
  CharacterTable characters;

  TargetFinderTests ()
    : finder(db), buildings(db), characters(db)
  {
    cb = [this] (const HexCoord& c, const proto::TargetId& t)
      {
        found.emplace_back (c, t);
      };
  }

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
   * Calls ProcessL1Targets with our callback and for a red "attacker",
   * looking only for enemies.
   */
  void
  ProcessEnemies (const HexCoord& centre, const HexCoord::IntT l1range)
  {
    finder.ProcessL1Targets (centre, l1range, Faction::RED, true, false, cb);
  }

  /**
   * Calls ProcessL1Targets with our callback and for a red "attacker",
   * looking only for friendlies.
   */
  void
  ProcessFriendlies (const HexCoord& centre, const HexCoord::IntT l1range)
  {
    finder.ProcessL1Targets (centre, l1range, Faction::RED, false, true, cb);
  }

  /**
   * Calls ProcessL1Targets with our callback and for a red "attacker",
   * looking for anyone (friendly and enemies).
   */
  void
  ProcessEveryone (const HexCoord& centre, const HexCoord::IntT l1range)
  {
    finder.ProcessL1Targets (centre, l1range, Faction::RED, true, true, cb);
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

} // anonymous namespace
} // namespace pxd
