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
Tests basic behaviour of foundations in the game.
"""

from pxtest import PXTest


class BuildingFoundationsTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.mainLogger.info ("Creating and preparing test character...")
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)
    self.changeCharacterVehicle ("domob", "chariot", ["sword"])
    pos = {"x": 0, "y": 0}
    self.dropLoot (pos, {"foo": 100})
    self.moveCharactersTo ({"domob": pos})
    self.getCharacters ()["domob"].sendMove ({"pu": {"f": {"foo": 4}}})
    self.generate (1)

    self.mainLogger.info ("Building foundations...")
    # It is actually possible to build *two* foundations (or more)
    # in a single block in the way done below.
    c = self.getCharacters ()["domob"]
    c.sendMove ({
      "fb": {"t": "huesli", "rot": 0},
      "xb": {},
    })
    c.sendMove ({
      "fb": {"t": "huesli", "rot": 0},
      "xb": {},
    })
    self.generate (1)

    buildings = self.getBuildings ()
    bIds = list (buildings.keys ())[-2:]
    for b in bIds:
      self.assertEqual (buildings[b].isFoundation (), True)
      self.assertEqual (buildings[b].getOwner (), "domob")
      self.assertEqual (buildings[b].getFaction (), "r")
      self.assertEqual (buildings[b].getType (), "huesli")

    self.mainLogger.info ("Dropping into foundation...")
    self.dropLoot (self.getCharacters ()["domob"].getPosition (),
                   {"zerospace": 5})
    self.getCharacters ()["domob"].sendMove ({
      "pu": {"f": {"zerospace": 5}},
      "eb": bIds[1],
    })
    self.generate (1)
    self.getCharacters ()["domob"].sendMove ({"drop": {"f": {"zerospace": 5}}})
    self.generate (1)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.getFungibleInventory (), {})
    b = self.getBuildings ()[bIds[1]]
    self.assertEqual (b.getConstructionInventory (), {"zerospace": 5})

    self.mainLogger.info ("Foundations are restricted in use...")
    self.giftCoins ({"domob": 1000000})
    self.getCharacters ()["domob"].sendMove ({
      "fit": [],
      "pu": {"f": {"zerospace": 10}},
    })
    self.generate (1)

    c = self.getCharacters ()["domob"]
    self.assertEqual (c.data["fitments"], ["sword"])
    self.assertEqual (c.getFungibleInventory (), {})

    self.setCharactersHP ({
      "domob": {"a": 10},
    })
    self.sendMove ("domob", {"s": [
      {"t": "fix", "c": c.getId (), "b": bIds[1]},
    ]})
    self.generate (10)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.getBusy (), None)
    self.assertEqual (c.data["combat"]["hp"]["current"]["armour"], 10)


if __name__ == "__main__":
  BuildingFoundationsTest ().main ()
