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
Tests basic refining service operations.
"""

from pxtest import PXTest, offsetCoord


class ServicesRefiningTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up initial situation...")
    self.build ("ancient1", None, {"x": 100, "y": 0}, 0)
    self.build ("ancient1", None, {"x": 0, "y": 100}, 0)
    buildings = [1001, 1002]
    for b in buildings:
      self.assertEqual (self.getBuildings ()[b].getType (), "ancient1")

    self.initAccount ("domob", "r")
    self.generate (1)
    self.giftCoins ({"domob": 10})

    for b in buildings:
      self.dropIntoBuilding (b, "domob", {"test ore": 3})

    self.generate (1)
    self.snapshot = self.env.snapshot ()

    self.mainLogger.info ("Performing refine operation...")
    self.sendMove ("domob", {"s": [
      {"b": b, "t": "ref", "i": "test ore", "n": 3}
    for b in buildings]})
    self.generate (1)

    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 0)
    b = self.getBuildings ()
    self.assertEqual (b[buildings[0]].getFungibleInventory ("domob"), {
      "bar": 2,
      "zerospace": 1,
    })
    self.assertEqual (b[buildings[1]].getFungibleInventory ("domob"), {
      "test ore": 3,
    })

    self.testMobileRefinery (buildings[0])

    self.generate (20)
    self.testReorg (buildings)

  def testMobileRefinery (self, bId):
    self.mainLogger.info ("Testing mobile refinery...")

    self.initAccount ("andy", "g")
    self.generate (1)
    # Due to the airdrop of vCHI, the new account will have 1000 coins
    # before we do the operation.
    self.dropIntoBuilding (bId, "andy", {"test ore": 6})

    self.createCharacters ("andy")
    self.generate (1)
    self.changeCharacterVehicle ("andy", "basetank", ["vhf refinery"])

    # We test refining with the mobile refinery inside a building, which
    # should still work (although it may not make too much sense in this
    # situation).
    b = self.getBuildings ()[bId]
    self.moveCharactersTo ({
      "andy": offsetCoord (b.getCentre (), {"x": -30, "y": 0}, False),
    })
    self.getCharacters ()["andy"].sendMove ({"eb": bId})
    self.generate (1)
    c = self.getCharacters ()["andy"]
    self.assertEqual (c.getBuildingId (), bId)

    c.sendMove ({"pu": {"f": {"test ore": 6}}})
    self.generate (1)
    self.getCharacters ()["andy"].sendMove ({"ref": {"i": "test ore", "n": 6}})
    self.generate (1)

    self.assertEqual (self.getAccounts ()["andy"].getBalance (), 990)
    self.assertEqual (self.getCharacters ()["andy"].getFungibleInventory (), {
      "bar": 2,
      "zerospace": 1,
    })

  def testReorg (self, buildings):
    self.mainLogger.info ("Testing reorg...")

    self.snapshot.restore ()

    self.sendMove ("domob", {"s": [
      {"b": b, "t": "ref", "i": "test ore", "n": 3}
    for b in buildings[::-1]]})
    self.generate (1)

    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 0)
    b = self.getBuildings ()
    self.assertEqual (b[buildings[1]].getFungibleInventory ("domob"), {
      "bar": 2,
      "zerospace": 1,
    })
    self.assertEqual (b[buildings[0]].getFungibleInventory ("domob"), {
      "test ore": 3,
    })


if __name__ == "__main__":
  ServicesRefiningTest ().main ()
