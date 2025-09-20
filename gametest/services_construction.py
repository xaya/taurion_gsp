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
Tests the "item/vehicle construction" building service.
"""

from pxtest import PXTest


class ServicesConstructionTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up initial situation...")
    self.build ("ancient1", None, {"x": 0, "y": 0}, 0)
    building = 1001
    self.assertEqual (self.getBuildings ()[building].getType (), "ancient1")

    self.initAccount ("domob", "r")
    self.generate (1)
    self.giftCoins ({"domob": 1000000})
    self.dropIntoBuilding (building, "domob", {"sword bpo": 1})
    self.dropIntoBuilding (building, "domob", {"zerospace": 50})
    self.dropIntoBuilding (building, "domob", {"chariot bpc": 2})
    self.generate (1)

    self.mainLogger.info ("Starting the construction operations...")
    self.sendMove ("domob", {"s": [
      {"b": building, "t": "bld", "i": "sword bpo", "n": 5},
      {"b": building, "t": "bld", "i": "chariot bpc", "n": 2},
    ]})

    self.generate (1)
    self.assertEqual (self.getAccounts ()["domob"].getBalance (),
                      1000000 - 5 * 100 - 2 * 100 * 100)
    b = self.getBuildings ()[building]
    self.assertEqual (b.getFungibleInventory ("domob"), {})
    _, start = self.env.getChainTip ()
    self.assertEqual (self.getRpc ("getongoings"), [
      {
        "id": 1002,
        "operation": "construct",
        "buildingid": building,
        "account": "domob",
        "start_height": start,
        "end_height": start + 5 * 10,
        "original": "sword bpo",
        "output": {"sword": 5},
      },
      {
        "id": 1003,
        "operation": "construct",
        "buildingid": building,
        "account": "domob",
        "start_height": start,
        "end_height": start + 1000,
        "output": {"chariot": 2},
      },
    ])

    self.mainLogger.info ("Partial construction from original...")
    self.generate (20)
    b = self.getBuildings ()[building]
    self.assertEqual (b.getFungibleInventory ("domob"), {
      "sword": 2,
    })
    self.assertEqual (self.getRpc ("getongoings"), [
      {
        "id": 1002,
        "operation": "construct",
        "buildingid": building,
        "account": "domob",
        "start_height": start,
        "end_height": start + 5 * 10,
        "original": "sword bpo",
        "output": {"sword": 3},
      },
      {
        "id": 1003,
        "operation": "construct",
        "buildingid": building,
        "account": "domob",
        "start_height": start,
        "end_height": start + 1000,
        "output": {"chariot": 2},
      },
    ])

    self.mainLogger.info ("Finishing the constructions...")
    self.generate (1000)
    b = self.getBuildings ()[building]
    self.assertEqual (b.getFungibleInventory ("domob"), {
      "sword bpo": 1,
      "sword": 5,
      "chariot": 2,
    })
    self.assertEqual (self.getRpc ("getongoings"), [])


if __name__ == "__main__":
  ServicesConstructionTest ().main ()
