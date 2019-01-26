#include "target.hpp"

#include "character.hpp"
#include "dbtest.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <gtest/gtest.h>

#include <sstream>
#include <vector>

namespace pxd
{
namespace
{

class TargetFinderTests : public DBTestWithSchema
{

private:

  /** Character table instance for inserting test characters.  */
  CharacterTable characters;

  /** Counter to generate unique names for test characters.  */
  unsigned characterCnt = 0;

protected:

  /** TargetFinder instance for use in the tests.  */
  TargetFinder finder;

  /**
   * Vector into which found targets will be inserted by the constructed
   * callback function cb.
   */
  std::vector<std::pair<HexCoord, proto::TargetId>> found;

  /** Callback that inserts targets into found.  */
  TargetFinder::ProcessingFcn cb;

  TargetFinderTests ()
    : characters(db), finder(db)
  {
    cb = [this] (const HexCoord& c, const proto::TargetId& t)
      {
        found.emplace_back (c, t);
      };
  }

  /**
   * Inserts a test character at the given position and with the given faction.
   * Returns the ID.
   */
  Database::IdT
  InsertCharacter (const HexCoord& pos, const Faction faction)
  {
    std::ostringstream name;
    name << "character " << ++characterCnt;

    auto h = characters.CreateNew ("domob", name.str (), faction);
    h->SetPosition (pos);

    return h->GetId ();
  }

};

TEST_F (TargetFinderTests, CharacterFactions)
{
  const auto idEnemy1 = InsertCharacter (HexCoord (1, 1), Faction::GREEN);
  InsertCharacter (HexCoord (-1, 1), Faction::RED);
  const auto idEnemy2 = InsertCharacter (HexCoord (0, 0), Faction::BLUE);

  finder.ProcessL1Targets (HexCoord (0, 0), 2, Faction::RED, cb);

  ASSERT_EQ (found.size (), 2);

  EXPECT_EQ (found[0].first, HexCoord (1, 1));
  EXPECT_EQ (found[0].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[0].second.id (), idEnemy1);

  EXPECT_EQ (found[1].first, HexCoord (0, 0));
  EXPECT_EQ (found[1].second.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (found[1].second.id (), idEnemy2);
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

  finder.ProcessL1Targets (centre, range, Faction::RED, cb);

  ASSERT_EQ (found.size (), expected.size ());
  for (unsigned i = 0; i < expected.size (); ++i)
    {
      EXPECT_EQ (found[i].first, expected[i].second);
      EXPECT_EQ (found[i].second.type (), proto::TargetId::TYPE_CHARACTER);
      EXPECT_EQ (found[i].second.id (), expected[i].first);
    }
}

} // anonymous namespace
} // namespace pxd
