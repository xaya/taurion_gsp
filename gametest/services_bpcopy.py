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
Tests the "blueprint copy" building service.
"""

from pxtest import PXTest


class ServicesBlueprintCopyTest (PXTest):

  def run (self):
    self.collectPremine ()
    self.splitPremine ()

    self.mainLogger.info ("Setting up initial situation...")
    self.build ("ancient1", None, {"x": 0, "y": 0}, 0)
    building = 1001
    self.assertEqual (self.getBuildings ()[building].getType (), "ancient1")

    self.initAccount ("domob", "r")
    self.generate (1)
    self.giftCoins ({"domob": 1000})
    self.dropIntoBuilding (building, "domob", {"sword bpo": 2})
    self.generate (1)

    # Start three operations.  The third will be invalid as the blueprints
    # are "blocked" by then.
    self.mainLogger.info ("Starting the copying operation...")
    self.sendMove ("domob", {"s": [
      {"b": building, "t": "cp", "i": "sword bpo", "n": 3},
      {"b": building, "t": "cp", "i": "sword bpo", "n": 2},
      {"b": building, "t": "cp", "i": "sword bpo", "n": 1},
    ]})

    self.generate (1)
    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 500)
    b = self.getBuildings ()[building]
    self.assertEqual (b.getFungibleInventory ("domob"), {})
    start = self.rpc.xaya.getblockcount ()
    self.assertEqual (self.getRpc ("getongoings"), [
      {
        "id": 1002,
        "operation": "bpcopy",
        "buildingid": building,
        "account": "domob",
        "start_height": start,
        "end_height": start + 30,
        "original": "sword bpo",
        "output": {"sword bpc": 3},
      },
      {
        "id": 1003,
        "operation": "bpcopy",
        "buildingid": building,
        "account": "domob",
        "start_height": start,
        "end_height": start + 20,
        "original": "sword bpo",
        "output": {"sword bpc": 2},
      },
    ])

    self.mainLogger.info ("Partial copying...")
    self.generate (20)
    b = self.getBuildings ()[building]
    self.assertEqual (b.getFungibleInventory ("domob"), {
      "sword bpo": 1,
      "sword bpc": 4,
    })
    self.assertEqual (self.getRpc ("getongoings"), [
      {
        "id": 1002,
        "operation": "bpcopy",
        "buildingid": building,
        "account": "domob",
        "start_height": start,
        "end_height": start + 30,
        "original": "sword bpo",
        "output": {"sword bpc": 1},
      },
    ])

    self.mainLogger.info ("Finishing the copying...")
    self.generate (50)
    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 500)
    b = self.getBuildings ()[building]
    self.assertEqual (b.getFungibleInventory ("domob"), {
      "sword bpo": 2,
      "sword bpc": 5,
    })
    self.assertEqual (self.getRpc ("getongoings"), [])


if __name__ == "__main__":
  ServicesBlueprintCopyTest ().main ()
