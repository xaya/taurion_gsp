#include "prospecting.hpp"

#include "database/dbtest.hpp"
#include "hexagonal/coord.hpp"

#include <gtest/gtest.h>

namespace pxd
{
namespace
{

class ProspectingTests : public DBTestWithSchema
{

protected:

  /** Character table used for interacting with the test database.  */
  CharacterTable characters;

  /** Regions table for the test.  */
  RegionsTable regions;

  /** Basemap instance for testing.  */
  const BaseMap map;

  ProspectingTests ()
    : characters(db), regions(db)
  {
    const auto h = characters.CreateNew ("domob", Faction::RED);
    CHECK_EQ (h->GetId (), 1);
  }

  /**
   * Returns a handle to the test character (for inspection and update).
   */
  CharacterTable::Handle
  GetTest ()
  {
    return characters.GetById (1);
  }

};

TEST_F (ProspectingTests, Works)
{
  const HexCoord pos(10, -20);
  const auto region = map.Regions ().GetRegionId (pos);

  auto c = GetTest ();
  c->SetPosition (pos);
  c->SetBusy (1);
  c->MutableProto ().mutable_prospection ();
  c.reset ();

  regions.GetById (region)->MutableProto ().set_prospecting_character (1);

  FinishProspecting (*GetTest (), regions, map);

  c = GetTest ();
  EXPECT_EQ (c->GetBusy (), 0);
  EXPECT_FALSE (c->GetProto ().has_prospection ());

  auto r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "domob");
}

} // anonymous namespace
} // namespace pxd
