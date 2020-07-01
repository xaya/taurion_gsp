#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020  Autonomous Worlds Ltd
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
Tests basic combat with buildings (turrets and destroying a building).
"""

from pxtest import PXTest


class BuildingsCombatTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.mainLogger.info ("Ancient buildings are neutral...")
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)
    ancientBuilding = 1
    b = self.getBuildings ()[ancientBuilding]
    self.assertEqual (b.getFaction (), "a")
    self.moveCharactersTo ({"domob": b.getCentre ()})
    self.generate (3)
    assert "target" not in self.getCharacters ()["domob"].data["combat"]
    assert "target" not in self.getBuildings ()[ancientBuilding].data["combat"]

    self.mainLogger.info ("Placing a turret...")
    self.generate (1)
    self.build ("r rt", "domob", {"x": 0, "y": 0}, rot=0)
    self.building = 1002
    self.assertEqual (self.getBuildings ()[self.building].getType (), "r rt")

    # Enter turret with a character, to test that it will get killed
    # correctly when the turret is destroyed.
    self.moveCharactersTo ({"domob": {"x": 1, "y": 0}})
    self.getCharacters ()["domob"].sendMove ({"eb": self.building})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["domob"].getBuildingId (),
                      self.building)

    self.mainLogger.info ("Placing an attacking character...")
    self.initAccount ("attacker", "g")
    self.generate (1)
    self.createCharacters ("attacker")
    self.generate (2)
    self.changeCharacterVehicle ("attacker", "light attacker")
    reorgBlk = self.rpc.xaya.getbestblockhash ()
    self.moveCharactersTo ({"attacker": {"x": 1, "y": 0}})

    self.mainLogger.info ("Killing both entities...")
    self.adminCommand ({"god": {"sethp": {"b": {
      "%s" % self.building: {"a": 1, "s": 0},
    }}}})
    self.setCharactersHP ({"attacker": {"a": 1, "s": 0}})
    self.generate (1)
    assert self.building not in self.getBuildings ()
    self.assertEqual (self.getCharacters (), {})

    self.generate (20)
    self.testReorg (reorgBlk)

  def testReorg (self, blk):
    self.mainLogger.info ("Testing reorg...")

    originalState = self.getGameState ()
    self.rpc.xaya.invalidateblock (blk)

    self.generate (5)
    self.assertEqual (set (self.getCharacters ().keys ()),
                      set (["attacker", "domob"]))
    assert self.building in self.getBuildings ()

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)
    

if __name__ == "__main__":
  BuildingsCombatTest ().main ()
