#!/usr/bin/env python

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
    self.moveCharactersTo ({"domob": {"x": -10, "y": 0}})
    self.getCharacters ()["domob"].sendMove ({"wp": [{"x": 10, "y": 0}]})
    self.generate (3)
    reorgBlock = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Placing a building...")
    self.build ("checkmark", None, {"x": 0, "y": 0}, rot=0)
    buildings = self.getBuildings ()
    self.assertEqual (len (buildings), 1)
    assert 2 in buildings
    self.assertEqual (buildings[2].getId (), 2)
    self.assertEqual (buildings[2].getType (), "checkmark")
    self.assertEqual (buildings[2].getFaction (), "a")
    self.assertEqual (buildings[2].getOwner (), None)
    self.assertEqual (buildings[2].data["rotationsteps"], 0)
    self.assertEqual (buildings[2].data["tiles"], [
      {"x": 0, "y": 0},
      {"x": 0, "y": 1},
      {"x": 0, "y": 2},
      {"x": 1, "y": 0},
    ])

    self.mainLogger.info ("Building will act as obstacle...")
    self.generate (20)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isMoving (), False)
    self.assertEqual (c.getPosition (), {"x": -1, "y": 0})

    self.testReorg (reorgBlock)

  def testReorg (self, blk):
    self.mainLogger.info ("Testing reorg...")

    originalState = self.getGameState ()
    self.rpc.xaya.invalidateblock (blk)

    self.generate (20)
    self.assertEqual (self.getBuildings (), {})
    self.assertEqual (self.getCharacters ()["domob"].getPosition (),
                      {"x": 10, "y": 0})

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)
    

if __name__ == "__main__":
  BuildingsBasicTest ().main ()
