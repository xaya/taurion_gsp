#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020-2025  Autonomous Worlds Ltd
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
Tests basic "armour repair" service operations.
"""

from pxtest import PXTest


class ServicesRepairTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up initial situation...")
    self.build ("ancient1", None, {"x": 0, "y": 0}, 0)
    building = 1001
    self.assertEqual (self.getBuildings ()[building].getType (), "ancient1")

    self.initAccount ("domob", "r")
    self.generate (1)
    self.giftCoins ({"domob": 100})
    self.createCharacters ("domob")
    self.generate (1)
    cId = self.getCharacters ()["domob"].getId ()
    self.setCharactersHP ({"domob": {"ma": 500, "a": 20}})
    self.moveCharactersTo ({"domob": {"x": 30, "y": 0}})
    self.getCharacters ()["domob"].sendMove ({"eb": building})
    self.generate (1)

    self.snapshot = self.env.snapshot ()

    self.mainLogger.info ("Starting repair...")
    self.sendMove ("domob", {"s": [
      {"b": building, "t": "fix", "c": cId},
    ]})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["domob"].getBusy (), {
      "operation": "armourrepair",
      "blocks": 5,
    })

    self.mainLogger.info ("Repairing character is busy...")
    self.getCharacters ()["domob"].sendMove ({"xb": {}})
    self.generate (4)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isInBuilding (), True)
    self.assertEqual (c.data["combat"]["hp"]["current"]["armour"], 20)
    self.assertEqual (c.getBusy (), {
      "operation": "armourrepair",
      "blocks": 1,
    })

    self.mainLogger.info ("Finishing the repair...")
    c.sendMove ({"xb": {}})
    self.generate (1)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isInBuilding (), False)
    self.assertEqual (c.data["combat"]["hp"]["current"]["armour"], 500)
    self.assertEqual (c.getBusy (), None)

    self.generate (20)
    self.testReorg (building, cId)

  def testReorg (self, building, cId):
    self.mainLogger.info ("Testing reorg...")

    self.snapshot.restore ()

    # We leave the building now instead of requesting a repair to create
    # an alternative reality.
    self.getCharacters ()["domob"].sendMove ({"xb": {}})
    self.generate (10)

    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isInBuilding (), False)
    self.assertEqual (c.data["combat"]["hp"]["current"]["armour"], 20)
    self.assertEqual (c.getBusy (), None)


if __name__ == "__main__":
  ServicesRepairTest ().main ()
