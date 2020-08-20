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
Tests some basic functionality of buildings:  Placing them in god mode,
retrieving them through RPC, and that they act as obstacles.
"""

from pxtest import PXTest


class BuildingsBasicTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.mainLogger.info ("Creating and moving test character...")
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)
    self.moveCharactersTo ({"domob": {"x": -20, "y": 0}})
    self.getCharacters ()["domob"].moveTowards ({"x": 20, "y": 0})
    self.generate (3)
    reorgBlock = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Checking for ancient buildings...")
    self.ancientBuildings = self.getBuildings ()
    assert len (self.ancientBuildings) >= 3
    self.assertEqual (self.ancientBuildings[1].getType (), "obelisk1")
    self.assertEqual (self.ancientBuildings[2].getType (), "obelisk2")
    self.assertEqual (self.ancientBuildings[3].getType (), "obelisk3")

    self.mainLogger.info ("Placing a building...")
    self.build ("checkmark", None, {"x": 0, "y": 0}, rot=0)
    buildings = self.getBuildings ()
    self.assertEqual (len (buildings), len (self.ancientBuildings) + 1)
    assert 1002 in buildings
    self.assertEqual (buildings[1002].getId (), 1002)
    self.assertEqual (buildings[1002].getType (), "checkmark")
    self.assertEqual (buildings[1002].getFaction (), "a")
    self.assertEqual (buildings[1002].getOwner (), None)
    self.assertEqual (buildings[1002].data["rotationsteps"], 0)
    self.assertEqual (buildings[1002].data["tiles"], [
      {"x": 0, "y": 0},
      {"x": 1, "y": 0},
      {"x": 0, "y": 1},
      {"x": 0, "y": 2},
    ])

    self.mainLogger.info ("Building will act as obstacle...")
    self.generate (10 + self.roConfig ().params.blocked_step_retries)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isMoving (), False)
    self.assertEqual (c.getPosition (), {"x": -1, "y": 0})

    self.testReorg (reorgBlock)

  def testReorg (self, blk):
    self.mainLogger.info ("Testing reorg...")

    originalState = self.getGameState ()
    self.rpc.xaya.invalidateblock (blk)

    self.generate (20)
    self.assertEqual ([b.data for b in self.getBuildings ().values ()],
                      [b.data for b in self.ancientBuildings.values ()])
    self.assertEqual (self.getCharacters ()["domob"].getPosition (),
                      {"x": 20, "y": 0})

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)
    

if __name__ == "__main__":
  BuildingsBasicTest ().main ()
