#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2025  Autonomous Worlds Ltd
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
Tests account inventories in buildings.
"""

from pxtest import PXTest


class BuildingsInventoriesTest (PXTest):

  def run (self):
    self.mainLogger.info ("Placing a building and some loot...")
    self.build ("checkmark", None, {"x": 0, "y": 0}, rot=0)
    building = 1001
    self.assertEqual (self.getBuildings ()[building].getType (), "checkmark")
    self.pos = {"x": 3, "y": 0}
    self.dropLoot (self.pos, {"foo": 1, "zerospace": 5})
    self.generate (1)
    self.snapshot = self.env.snapshot ()

    self.mainLogger.info ("Entering building with character...")
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)
    self.moveCharactersTo ({"domob": self.pos})
    self.getCharacters ()["domob"].sendMove ({
      "pu": {"f": {"foo": 10, "zerospace": 10}},
      "eb": building,
    })
    self.generate (1)

    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isInBuilding (), True)
    self.assertEqual (c.getFungibleInventory (), {
      "foo": 1,
      "zerospace": 5,
    })
    self.assertEqual (self.getLoot (self.pos), {})

    self.mainLogger.info ("Dropping loot in building...")
    self.getCharacters ()["domob"].sendMove ({
      "drop": {"f": {"foo": 10, "zerospace": 10}},
    })
    self.generate (1)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.getFungibleInventory (), {})
    b = self.getBuildings ()[building]
    self.assertEqual (b.getFungibleInventory ("domob"), {
      "foo": 1,
      "zerospace": 5,
    })

    self.mainLogger.info ("Picking up from building and leaving...")
    self.getCharacters ()["domob"].sendMove ({
      "pu": {"f": {"foo": 10, "zerospace": 10}},
      "xb": {},
    })
    self.generate (1)

    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isInBuilding (), False)
    self.assertEqual (c.getFungibleInventory (), {
      "foo": 1,
      "zerospace": 5,
    })
    b = self.getBuildings ()[building]
    self.assertEqual (b.getFungibleInventory ("domob"), {})

    self.testReorg ()

  def testReorg (self):
    self.mainLogger.info ("Testing reorg...")

    self.snapshot.restore ()

    self.assertEqual (self.getLoot (self.pos), {
      "foo": 1,
      "zerospace": 5,
    })
    

if __name__ == "__main__":
  BuildingsInventoriesTest ().main ()
