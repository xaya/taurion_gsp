#include "logic.hpp"

#include "params.hpp"
#include "protoutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
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

  /** Base map instance for testing.  */
  const BaseMap map;

  /** Random instance for testing.  */
  xaya::Random rnd;

  /** Test parameters.  */
  const Params params;

protected:

  /** Character table for use in tests.  */
  CharacterTable characters;

  PXLogicTests ()
    : params(xaya::Chain::MAIN), characters(db)
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
  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
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
  auto c = characters.CreateNew ("domob", "foo", Faction::RED);
  const auto id1 = c->GetId ();
  auto* attack = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  attack->set_range (10);
  attack->set_max_damage (1);
  c.reset ();

  c = characters.CreateNew ("domob", "bar", Faction::GREEN);
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
  auto c = characters.CreateNew ("domob", "attacker", Faction::RED);
  auto* attack = c->MutableProto ().mutable_combat_data ()->add_attacks ();
  attack->set_range (1);
  attack->set_max_damage (1);
  c.reset ();

  c = characters.CreateNew ("domob", "target", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->MutableHP ().set_armour (100);
  c->MutableProto ().mutable_combat_data ();
  c.reset ();

  UpdateState ("[]");
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 100);
  UpdateState ("[]");
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 99);
}

} // namespace pxd
