#include "gamestatejson.hpp"

#include "database/character.hpp"

#include "database/dbtest.hpp"

#include <gtest/gtest.h>

#include <json/json.h>

#include <sstream>
#include <string>

namespace pxd
{
namespace
{

class GameStateJsonTests : public DBTestWithSchema
{

protected:

  /**
   * Expects that the current state equals to the given one, after parsing
   * the expected state's string as JSON.  In other words, not strings are
   * compared, but the resulting JSON objects.
   */
  void
  ExpectStateJson (const std::string& expectedStr)
  {
    Json::Value expected;
    std::istringstream in(expectedStr);
    in >> expected;

    const Json::Value actual = GameStateToJson (*db);
    ASSERT_EQ (actual, expected);
  }

};

TEST_F (GameStateJsonTests, Characters)
{
  CharacterTable tbl(*db);

  auto c = tbl.CreateNew ("domob", "foo");
  c->SetPosition (HexCoord (-5, 2));
  c.reset ();

  /*
  tbl.CreateNew ("andy", u8"äöü");
  */

  ExpectStateJson (u8R"({
    "characters":
      [
        {
          "id": 1, "name": "foo", "owner": "domob",
          "position": {"x": -5, "y": 2}
        }
      ]
  })");
}

} // anonymous namespace
} // namespace pxd
