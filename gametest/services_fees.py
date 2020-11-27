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
Tests service fees for building owners.
"""

from pxtest import PXTest


class ServicesFeesTest (PXTest):

  def run (self):
    self.collectPremine ()
    self.splitPremine ()

    self.mainLogger.info ("Setting up initial situation...")
    self.initAccount ("domob", "r")
    self.initAccount ("andy", "r")
    self.generate (1)
    self.build ("ancient1", None, {"x": 0, "y": 0}, 0)
    self.build ("ancient1", "domob", {"x": 100, "y": 0}, 0)
    ancient = 1001
    domob = 1002
    self.assertEqual (self.getBuildings ()[ancient].getOwner (), None)
    self.assertEqual (self.getBuildings ()[domob].getOwner (), "domob")

    self.dropIntoBuilding (ancient, "andy", {"test ore": 3})
    self.dropIntoBuilding (domob, "andy", {"test ore": 3})
    self.dropIntoBuilding (domob, "domob", {"test ore": 3})

    self.mainLogger.info ("Setting service fee...")
    self.getBuildings ()[domob].sendMove ({"sf": 50})
    self.generate (11)
    b = self.getBuildings ()[domob]
    self.assertEqual (b.data["config"]["servicefee"], 50)

    self.mainLogger.info ("Using 'free' services...")
    self.giftCoins ({"domob": 10, "andy": 10})
    self.sendMove ("andy", {"s": [
      {"b": ancient, "t": "ref", "i": "test ore", "n": 3},
    ]})
    self.sendMove ("domob", {"s": [
      {"b": domob, "t": "ref", "i": "test ore", "n": 3},
    ]})
    self.generate (1)
    self.assertEqual (self.getAccounts ()["andy"].getBalance (), 0)
    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 0)
    b = self.getBuildings ()
    self.assertEqual (b[ancient].getFungibleInventory ("andy"), {
      "bar": 2,
      "zerospace": 1,
    })
    self.assertEqual (b[domob].getFungibleInventory ("domob"), {
      "bar": 2,
      "zerospace": 1,
    })

    self.mainLogger.info ("Using service with fee...")
    self.giftCoins ({"andy": 20})
    self.sendMove ("andy", {"s": [
      {"b": domob, "t": "ref", "i": "test ore", "n": 3},
    ]})
    self.generate (1)
    self.assertEqual (self.getAccounts ()["andy"].getBalance (), 5)
    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 5)
    b = self.getBuildings ()
    self.assertEqual (b[domob].getFungibleInventory ("andy"), {
      "bar": 2,
      "zerospace": 1,
    })


if __name__ == "__main__":
  ServicesFeesTest ().main ()
