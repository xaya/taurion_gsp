#include "fighter.hpp"

#include "character.hpp"
#include "dbtest.hpp"
#include "faction.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class FighterTests : public DBTestWithSchema
{

protected:

  /** Character table instance used in testing.  */
  CharacterTable characters;

  /** FighterTable used for testing.  */
  FighterTable tbl;

  FighterTests ()
    : characters(db), tbl(characters)
  {}

};

TEST_F (FighterTests, Characters)
{
  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (2, 5));
  c->MutableProto ().mutable_combat_data ()->add_attacks ();
  c->MutableHP ().set_armour (10);
  c.reset ();

  c = characters.CreateNew ("domob", "bar", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->MutableProto ().mutable_target ()->set_id (42);
  c.reset ();

  unsigned cnt = 0;
  tbl.ProcessAll ([this, &cnt] (Fighter f)
    {
      ++cnt;

      switch (f.GetFaction ())
        {
        case Faction::RED:
          {
            EXPECT_EQ (f.GetPosition (), HexCoord (2, 5));
            EXPECT_EQ (f.GetCombatData ().attacks_size (), 1);

            auto& hp = f.MutableHP ();
            EXPECT_EQ (hp.armour (), 10);
            hp.set_armour (5);

            proto::TargetId t;
            t.set_id (5);
            f.SetTarget (t);

            break;
          }

        case Faction::GREEN:
          f.ClearTarget ();
          break;

        default:
          FAIL ()
              << "Unexpected faction: " << static_cast<int> (f.GetFaction ());
        }
    });
  EXPECT_EQ (cnt, 2);

  c = characters.GetById (id1);
  EXPECT_EQ (c->GetProto ().target ().id (), 5);
  EXPECT_EQ (c->GetHP ().armour (), 5);
  c.reset ();

  EXPECT_FALSE (characters.GetById (id2)->GetProto ().has_target ());
}

} // anonymous namespace
} // namespace pxd
