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
Tests construction of buildings.
"""

from pxtest import PXTest


class BuildingConstructionTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.mainLogger.info ("Creating and preparing test character...")
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)
    self.changeCharacterVehicle ("domob", "chariot")
    pos = {"x": 0, "y": 0}
    inv = {"foo": 100, "zerospace": 100}
    self.dropLoot (pos, inv)
    self.moveCharactersTo ({"domob": pos})
    self.getCharacters ()["domob"].sendMove ({"pu": {"f": inv}})
    self.generate (1)

    self.mainLogger.info ("Building foundation...")
    c = self.getCharacters ()["domob"]
    c.sendMove ({
      "fb": {"t": "huesli", "rot": 0},
      "drop": {"f": {"foo": 100}},
    })
    self.generate (1)

    buildings = self.getBuildings ()
    bId = list (buildings.keys ())[-1]
    self.assertEqual (buildings[bId].isFoundation (), True)
    self.assertEqual (buildings[bId].getConstructionInventory (), {
      "foo": 98,
    })
    self.assertEqual (buildings[bId].getOngoingConstruction (), None)

    self.mainLogger.info ("Starting the construction...")
    self.getCharacters ()["domob"].sendMove ({
      "drop": {"f": {"zerospace": 100}},
    })
    self.generate (10)
    b = self.getBuildings ()[bId]
    self.assertEqual (b.isFoundation (), True)
    self.assertEqual (b.getConstructionInventory (), {
      "foo": 98,
      "zerospace": 100,
    })
    self.assertEqual (b.getOngoingConstruction (), {
      "operation": "build",
      "blocks": 1,
    })

    self.mainLogger.info ("Finishing construction...")
    self.generate (1)
    b = self.getBuildings ()[bId]
    self.assertEqual (b.isFoundation (), False)
    self.assertEqual (b.getFungibleInventory ("domob"), {
      "foo": 95,
      "zerospace": 90,
    })
    self.assertEqual (b.getOngoingConstruction (), None)


if __name__ == "__main__":
  BuildingConstructionTest ().main ()
