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

from pxtest import PXTest

"""
Tests the getserviceinfo RPC command.
"""


class GetServiceInfoTest (PXTest):

  def run (self):

    def getserviceinfo (name, op):
      return self.getRpc ("getserviceinfo", name=name, op=op)

    self.mainLogger.info ("Setting up basic situation...")
    self.initAccount ("domob", "r")
    self.initAccount ("andy", "r")
    self.generate (1)

    self.build ("ancient1", "domob", {"x": 0, "y": 0}, 0)
    building = 1001
    self.assertEqual (self.getBuildings ()[building].getOwner (), "domob")

    self.getBuildings ()[building].sendMove ({"sf": 50})
    self.generate (11)
    b = self.getBuildings ()[building]
    self.assertEqual (b.data["config"]["servicefee"], 50)

    self.dropIntoBuilding (building, "andy", {"test ore": 3})

    self.giftCoins ({"domob": 100})
    self.createCharacters ("domob")
    self.generate (1)
    cId = self.getCharacters ()["domob"].getId ()
    self.moveCharactersTo ({"domob": {"x": 30, "y": 0}})
    self.getCharacters ()["domob"].sendMove ({"eb": building})
    self.generate (1)

    self.mainLogger.info ("Testing invalid account...")
    self.expectError (-2, "account does not exist",
                      getserviceinfo, "invalid", {})

    self.mainLogger.info ("Testing fully invalid service...")
    self.assertEqual (getserviceinfo ("andy", {"foo": "bar"}), None)

    self.mainLogger.info ("Testing validly parsed service...")
    op = {"b": building, "t": "ref", "i": "test ore", "n": 3}
    self.assertEqual (getserviceinfo ("andy", op), {
      "type": "refining",
      "building": building,
      "input": {"test ore": 3},
      "output": {"bar": 2, "zerospace": 1},
      "cost": {"base": 10, "fee": 5},
      "valid": False,
    })
    self.giftCoins ({"andy": 100})
    self.assertEqual (getserviceinfo ("andy", op)["valid"], True)

    # This used to crash the GSP because of its zero cost.
    self.mainLogger.info ("Testing zero-cost repair...")
    op = {"b": building, "t": "fix", "c": cId}
    self.assertEqual (getserviceinfo ("andy", op), {
      "type": "armourrepair",
      "building": building,
      "character": cId,
      "cost": {"base": 0, "fee": 0},
      "valid": False,
    })


if __name__ == "__main__":
  GetServiceInfoTest ().main ()
