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
Tests basic refining service operations.
"""

from pxtest import PXTest


class ServicesRefiningTest (PXTest):

  def run (self):
    self.collectPremine ()
    self.splitPremine ()

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
      self.dropIntoBuilding (b, "domob", {"foo": 3})

    self.generate (1)
    reorgBlk = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Performing refine operation...")
    self.sendMove ("domob", {"s": [
      {"b": b, "t": "ref", "i": "foo", "n": 3}
    for b in buildings]})
    self.generate (1)

    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 0)
    b = self.getBuildings ()
    self.assertEqual (b[buildings[0]].getFungibleInventory ("domob"), {
      "bar": 2,
      "zerospace": 1,
    })
    self.assertEqual (b[buildings[1]].getFungibleInventory ("domob"), {
      "foo": 3,
    })

    self.generate (20)
    self.testReorg (reorgBlk, buildings)

  def testReorg (self, blk, buildings):
    self.mainLogger.info ("Testing reorg...")

    originalState = self.getGameState ()
    self.rpc.xaya.invalidateblock (blk)

    self.sendMove ("domob", {"s": [
      {"b": b, "t": "ref", "i": "foo", "n": 3}
    for b in buildings[::-1]]})
    self.generate (1)

    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 0)
    b = self.getBuildings ()
    self.assertEqual (b[buildings[1]].getFungibleInventory ("domob"), {
      "bar": 2,
      "zerospace": 1,
    })
    self.assertEqual (b[buildings[0]].getFungibleInventory ("domob"), {
      "foo": 3,
    })

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)


if __name__ == "__main__":
  ServicesRefiningTest ().main ()
