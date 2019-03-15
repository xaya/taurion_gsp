#include "logic.hpp"

#include "params.hpp"
#include "protoutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
#include "database/region.hpp"
#include "hexagonal/coord.hpp"
#include "mapdata/basemap.hpp"

#include <xayagame/hash.hpp>
#include <xayagame/random.hpp>

#include <gtest/gtest.h>

#include <json/json.h>

#include <sstream>
#include <string>

namespace pxd
{

/**
 * Test fixture for testing PXLogic::UpdateState.  It sets up a test database
 * independent from SQLiteGame, so that we can more easily test custom
 * situations as needed.
 */
class PXLogicTests : public DBTestWithSchema
{

private:

  /** Random instance for testing.  */
  xaya::Random rnd;

  /** Test parameters.  */
  const Params params;

protected:

  /** Base map instance for testing.  */
  const BaseMap map;

  /** Character table for use in tests.  */
  CharacterTable characters;

  /** Regions table for testing.  */
  RegionsTable regions;

  PXLogicTests ()
    : params(xaya::Chain::MAIN), characters(db), regions(db)
  {
    xaya::SHA256 seed;
    seed << "test seed";
    rnd.Seed (seed.Finalise ());
  }

  /**
   * Calls PXLogic::UpdateState with our test instances of the database,
   * params and RNG.  The given string is parsed as JSON array and used
   * as moves in the block data.
   */
  void
  UpdateState (const std::string& movesStr)
  {
    Json::Value blockData(Json::objectValue);

    std::istringstream in(movesStr);
    in >> blockData["moves"];

    PXLogic::UpdateState (db, rnd, params, map, blockData);
  }

};

TEST_F (PXLogicTests, WaypointsBeforeMovement)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPartialStep (1000);
  auto& pb = c->MutableProto ();
  pb.mutable_combat_data ();
  pb.mutable_movement ()->add_waypoints ()->set_x (5);
  c.reset ();

  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"wp": [{"x": -1, "y": 0}]}}}
    }
  ])");

  EXPECT_EQ (characters.GetById (1)->GetPosition (), HexCoord (0, 0));
  UpdateState ("[]");
  EXPECT_EQ (characters.GetById (1)->GetPosition (), HexCoord (-1, 0));
}

TEST_F (PXLogicTests, MovementBeforeTargeting)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  auto* attack = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  attack->set_range (10);
  attack->set_max_damage (1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (11, 0));
  c->MutableProto ().mutable_combat_data ();
  c.reset ();

  UpdateState ("[]");

  ASSERT_EQ (characters.GetById (id2)->GetPosition (), HexCoord (11, 0));
  ASSERT_FALSE (characters.GetById (id1)->GetProto ().has_target ());

  c = characters.GetById (id2);
  auto* wp = c->MutableProto ().mutable_movement ()->mutable_waypoints ();
  c->SetPartialStep (500);
  *wp->Add () = CoordToProto (HexCoord (0, 0));
  c.reset ();

  UpdateState ("[]");

  ASSERT_EQ (characters.GetById (id2)->GetPosition (), HexCoord (10, 0));
  c = characters.GetById (id1);
  ASSERT_TRUE (c->GetProto ().has_target ());
  const auto& t = c->GetProto ().target ();
  EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (t.id (), id2);
}

TEST_F (PXLogicTests, DamageInNextRound)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  auto* attack = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  attack->set_range (1);
  attack->set_max_damage (1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->MutableHP ().set_armour (100);
  c->MutableProto ().mutable_combat_data ();
  c.reset ();

  UpdateState ("[]");
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 100);
  UpdateState ("[]");
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 99);
}

TEST_F (PXLogicTests, DamageKillsRegeneration)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  auto* attack = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  attack->set_range (1);
  attack->set_max_damage (1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->MutableProto ().mutable_combat_data ();
  c.reset ();

  /* Progress one round forward to target.  */
  UpdateState ("[]");

  /* Update the target character so that it will be killed with the attack,
     but would regenerate HP if that were done before applying damage.  */
  c = characters.GetById (idTarget);
  ASSERT_TRUE (c != nullptr);
  auto* cd = c->MutableProto ().mutable_combat_data ();
  cd->set_shield_regeneration_mhp (2000);
  cd->mutable_max_hp ()->set_shield (100);
  c->MutableHP ().set_shield (1);
  c->MutableHP ().set_armour (0);
  c.reset ();

  /* Now the attack should kill the target before it can regenerate.  */
  UpdateState ("[]");
  EXPECT_TRUE (characters.GetById (idTarget) == nullptr);
}

TEST_F (PXLogicTests, ProspectingBeforeMovement)
{
  /* This should test that prospecting is started before processing
     movement.  In other words, if a character is about to move to the
     next region when a "prospect" command hits, then prospecting should
     be started at the "old" region.  For this, we need two coordinates
     next to each other but in different regions.  */
  HexCoord pos1, pos2;
  RegionMap::IdT region1, region2;
  for (HexCoord::IntT x = 0; ; ++x)
    {
      pos1 = HexCoord (x, 0);
      region1 = map.Regions ().GetRegionId (pos1);
      pos2 = HexCoord (x + 1, 0);
      region2 = map.Regions ().GetRegionId (pos2);
      if (region1 != region2)
        break;
    }
  CHECK_NE (region1, region2);
  CHECK_EQ (HexCoord::DistanceL1 (pos1, pos2), 1);
  LOG (INFO)
      << "Neighbouring coordinates " << pos1 << " and " << pos2
      << " are in differing regions " << region1 << " and " << region2;

  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos1);
  c->SetPartialStep (1000);
  auto& pb = c->MutableProto ();
  pb.mutable_combat_data ();
  *pb.mutable_movement ()->add_waypoints () = CoordToProto (pos2);
  c.reset ();

  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");

  c = characters.GetById (1);
  EXPECT_EQ (c->GetPosition (), pos1);
  EXPECT_EQ (c->GetBusy (), 10);
  EXPECT_TRUE (c->GetProto ().has_prospection ());

  auto r = regions.GetById (region1);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  r = regions.GetById (region2);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (PXLogicTests, ProspectingUserKilled)
{
  const HexCoord pos(5, 5);
  const auto region = map.Regions ().GetRegionId (pos);

  /* Set up characters such that one is killing the other on the next round.  */
  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos);
  auto* attack = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  attack->set_range (1);
  attack->set_max_damage (1);
  c.reset ();

  c = characters.CreateNew ("domob", Faction::GREEN);
  ASSERT_EQ (c->GetId (), 2);
  c->SetPosition (pos);
  auto* cd = c->MutableProto ().mutable_combat_data ();
  cd->mutable_max_hp ()->set_shield (100);
  c->MutableHP ().set_shield (1);
  c->MutableHP ().set_armour (0);
  c.reset ();

  /* Progress one round forward to target and also start prospecting
     with the character that will be killed.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"2": {"prospect": {}}}}
    }
  ])");

  c = characters.GetById (2);
  EXPECT_EQ (c->GetBusy (), 10);
  EXPECT_TRUE (c->GetProto ().has_prospection ());

  /* Make sure that the prospecting operation would be finished on the next
     step (but it won't be as the character is killed).  */
  c->SetBusy (1);
  c.reset ();

  auto r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 2);

  /* Process another round, where the prospecting character is killed.  Thus
     the other is able to start prospecting at the same spot.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");

  EXPECT_TRUE (characters.GetById (2) == nullptr);

  c = characters.GetById (1);
  EXPECT_EQ (c->GetBusy (), 10);
  EXPECT_TRUE (c->GetProto ().has_prospection ());

  r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  EXPECT_FALSE (r->GetProto ().has_prospection ());
}

TEST_F (PXLogicTests, FinishingProspecting)
{
  const HexCoord pos(5, 5);
  const auto region = map.Regions ().GetRegionId (pos);

  auto c = characters.CreateNew ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos);
  c->MutableProto ().mutable_combat_data ();
  c.reset ();

  /* Start prospecting with that character.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");
  EXPECT_EQ (characters.GetById (1)->GetBusy (), 10);

  /* Process blocks until the operation is nearly done.  */
  for (unsigned i = 0; i < 9; ++i)
    UpdateState ("[]");
  EXPECT_EQ (characters.GetById (1)->GetBusy (), 1);

  auto r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  EXPECT_FALSE (r->GetProto ().has_prospection ());

  /* Process the last block which finishes prospecting.  We should be able
     to do a movement command right away as well, since the busy state is
     processed before the moves.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"wp": [{"x": 0, "y": 0}]}}}
    }
  ])");

  c = characters.GetById (1);
  EXPECT_EQ (c->GetBusy (), 0);
  EXPECT_FALSE (c->GetProto ().has_prospection ());
  EXPECT_TRUE (c->GetProto ().has_movement ());

  r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "domob");
}

} // namespace pxd
