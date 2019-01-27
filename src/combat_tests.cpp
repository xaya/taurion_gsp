#include "combat.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
#include "hexagonal/coord.hpp"

#include <xayagame/hash.hpp>
#include <xayagame/random.hpp>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <map>
#include <sstream>
#include <vector>

namespace pxd
{
namespace
{

class TargetSelectionTests : public DBTestWithSchema
{

protected:

  /** Character table for access to characters in the test.  */
  CharacterTable characters;

  /** Random instance for finding the targets.  */
  xaya::Random rnd;

  TargetSelectionTests ()
    : characters(db)
  {
    xaya::SHA256 seed;
    seed << "random seed";
    rnd.Seed (seed.Finalise ());
  }

};

TEST_F (TargetSelectionTests, NoTargets)
{
  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto id1 = c->GetId ();
  c->SetPosition (HexCoord (-10, 0));
  c->MutableProto ().mutable_target ();
  c.reset ();

  c = characters.CreateNew ("domob", "bar", Faction::RED);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (-10, 1));
  c->MutableProto ().mutable_target ();
  c.reset ();

  c = characters.CreateNew ("domob", "baz", Faction::GREEN);
  const auto id3 = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  c->MutableProto ().mutable_target ();
  c.reset ();

  FindCombatTargets (db, rnd);

  EXPECT_FALSE (characters.GetById (id1)->GetProto ().has_target ());
  EXPECT_FALSE (characters.GetById (id2)->GetProto ().has_target ());
  EXPECT_FALSE (characters.GetById (id3)->GetProto ().has_target ());
}

TEST_F (TargetSelectionTests, ClosestTarget)
{
  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  c.reset ();

  c = characters.CreateNew ("domob", "bar", Faction::GREEN);
  c->SetPosition (HexCoord (2, 2));
  c.reset ();

  c = characters.CreateNew ("domob", "baz", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (HexCoord (1, 1));
  c.reset ();

  /* Since target selection is randomised, run this multiple times to ensure
     that we always pick the same target (single closest one).  */
  for (unsigned i = 0; i < 100; ++i)
    {
      FindCombatTargets (db, rnd);

      c = characters.GetById (idFighter);
      const auto& pb = c->GetProto ();
      ASSERT_TRUE (pb.has_target ());
      EXPECT_EQ (pb.target ().type (), proto::TargetId::TYPE_CHARACTER);
      EXPECT_EQ (pb.target ().id (), idTarget);
    }
}

TEST_F (TargetSelectionTests, Randomisation)
{
  constexpr unsigned nTargets = 5;
  constexpr unsigned rolls = 1000;
  constexpr unsigned threshold = rolls / nTargets * 80 / 100;

  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto idFighter = c->GetId ();
  c->SetPosition (HexCoord (0, 0));
  c.reset ();

  std::map<Database::IdT, unsigned> targetMap;
  for (unsigned i = 0; i < nTargets; ++i)
    {
      std::ostringstream name;
      name << "target " << i;

      c = characters.CreateNew ("domob", name.str (), Faction::GREEN);
      targetMap.emplace (c->GetId (), i);
      c->SetPosition (HexCoord (1, 1));
      c.reset ();
    }
  ASSERT_EQ (targetMap.size (), nTargets);

  std::vector<unsigned> cnt(nTargets);
  for (unsigned i = 0; i < rolls; ++i)
    {
      FindCombatTargets (db, rnd);

      c = characters.GetById (idFighter);
      const auto& pb = c->GetProto ();
      ASSERT_TRUE (pb.has_target ());
      EXPECT_EQ (pb.target ().type (), proto::TargetId::TYPE_CHARACTER);

      const auto mit = targetMap.find (pb.target ().id ());
      ASSERT_NE (mit, targetMap.end ());
      ++cnt[mit->second];
    }

  for (unsigned i = 0; i < nTargets; ++i)
    {
      LOG (INFO) << "Target " << i << " was selected " << cnt[i] << " times";
      EXPECT_GE (cnt[i], threshold);
    }
}

} // anonymous namespace
} // namespace pxd
