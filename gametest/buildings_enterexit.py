#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2020  Autonomous Worlds Ltd
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""
Tests entering and exiting buildings with characters.
"""

from pxtest import PXTest


class BuildingsEnterExitTest (PXTest):

  def run (self):
    self.collectPremine ()
    self.splitPremine ()

    self.mainLogger.info ("Placing a building...")
    self.build ("checkmark", None, {"x": 0, "y": 0}, rot=0)
    building = 1001
    self.assertEqual (self.getBuildings ()[building].getType (), "checkmark")

    self.mainLogger.info ("Entering building with character...")
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)
    self.changeCharacterVehicle ("domob", "light attacker")
    self.moveCharactersTo ({"domob": {"x": 20, "y": 0}})
    c = self.getCharacters ()["domob"]
    c.sendMove ({
      "wp": c.findPath ({"x": 3, "y": 0}),
      "eb": building
    })
    self.generate (2)
    reorgBlock = self.rpc.xaya.getbestblockhash ()

    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isInBuilding (), False)
    self.assertEqual (c.data["enterbuilding"], building)

    self.generate (10)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isInBuilding (), True)
    self.assertEqual (c.getBuildingId (), building)

    self.mainLogger.info ("Exiting building...")
    self.getCharacters ()["domob"].sendMove ({"xb": {}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["domob"].isInBuilding (), False)

    self.mainLogger.info ("Attacking and being inside buildings...")
    self.initAccount ("andy", "g")
    self.createCharacters ("andy")
    self.generate (1)
    self.changeCharacterVehicle ("andy", "light attacker")
    self.moveCharactersTo ({
      "domob": {"x": 5, "y": 0},
      "andy": {"x": 5, "y": 0},
    })

    chars = self.getCharacters ()
    self.assertEqual (chars["andy"].data["combat"]["target"]["id"],
                      chars["domob"].getId ())
    self.assertEqual (chars["domob"].data["combat"]["target"]["id"],
                      chars["andy"].getId ())

    chars["andy"].sendMove ({"eb": building})
    self.generate (1)
    chars = self.getCharacters ()
    self.assertEqual (chars["andy"].isInBuilding (), True)
    assert "target" not in chars["andy"].data["combat"]
    assert "target" not in chars["domob"].data["combat"]

    # Before doing the reorg, make sure this is the longest chain.
    self.generate (100)
    self.testReorg (reorgBlock)

  def testReorg (self, blk):
    self.mainLogger.info ("Testing reorg...")

    originalState = self.getGameState ()
    self.rpc.xaya.invalidateblock (blk)

    self.getCharacters ()["domob"].sendMove ({"eb": None})
    self.generate (20)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isInBuilding (), False)
    self.assertEqual (c.getPosition (), {"x": 3, "y": 0})
    assert "enterbuilding" not in c.data

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)
    

if __name__ == "__main__":
  BuildingsEnterExitTest ().main ()
