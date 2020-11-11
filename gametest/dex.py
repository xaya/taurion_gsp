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
Tests DEX operations (trading of assets inside buildings).
"""

from pxtest import PXTest


class DexTest (PXTest):

  def expectBalances (self, expected):
    """
    Verifies that a set of accounts has given balances, as stated
    in the dictionary passed in.  The expected value may be a tuple,
    in which case it is (available, reserved).  If it is just a number,
    then we expect the reserved balance to be zero.
    """

    acc = self.getAccounts ()
    for k, v in expected.items ():
      if type (v) == tuple:
        expectedAvailable, expectedReserved = v
      else:
        expectedAvailable = v
        expectedReserved = 0

      if k not in acc:
        self.assertEqual (expectedAvailable, 0)
        self.assertEqual (expectedReserved, 0)
      else:
        self.assertEqual (acc[k].getBalance ("available"), expectedAvailable)
        self.assertEqual (acc[k].getBalance ("reserved"), expectedReserved)
        total = expectedAvailable + expectedReserved
        self.assertEqual (acc[k].getBalance ("total"), total)

  def expectItems (self, building, account, expected):
    """
    Checks that the item balances reported for a given account inside
    some building match the expectations.  The expected values can
    be tuples (available, reserved) or single numbers; if they are just
    numbers, the reserved value is assumed zero.
    """

    b = self.getBuildings ()[building]
    invAvailable = b.getFungibleInventory (account, "available")
    invReserved = b.getFungibleInventory (account, "reserved")

    for k, v in expected.items ():
      if type (v) == tuple:
        self.assertEqual ((invAvailable[k], invReserved[k]), v)
      else:
        self.assertEqual (invAvailable[k], v)
        self.assertEqual (invReserved[k], 0)

  def run (self):
    self.collectPremine ()
    self.splitPremine ()

    self.mainLogger.info ("Setting up test situation...")
    # No need to initialise accounts.  DEX operations also work
    # without a chosen faction.  Only the building owner has to
    # have a faction.
    self.initAccount ("building", "r")
    self.generate (1)
    self.build ("checkmark", "building", {"x": 0, "y": 0}, rot=0)
    buildings = self.getBuildings ()
    self.buildingId = max (buildings.keys ())
    self.assertEqual (buildings[self.buildingId].getOwner (), "building")
    buildings[self.buildingId].sendMove ({"xf": 1_000})
    self.giftCoins ({"buyer": 1_000})
    self.dropIntoBuilding (self.buildingId, "seller", {"foo": 10, "bar": 20})

    self.generate (1)
    reorgBlk = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Transferring assets...")
    self.sendMove ("seller", {"x": [{
      "b": self.buildingId,
      "i": "bar",
      "n": 5,
      "t": "gifted",
    }]})
    self.generate (1)
    self.expectBalances ({
      "buyer": 1_000,
      "seller": 0,
      "building": 0,
      "gifted": 0,
    })
    self.expectItems (self.buildingId, "buyer", {"foo": 0, "bar": 0})
    self.expectItems (self.buildingId, "seller", {"foo": 10, "bar": 15})
    self.expectItems (self.buildingId, "gifted", {"foo": 0, "bar": 5})

    self.mainLogger.info ("Placing orders...")
    firstOrderId = self.buildingId + 1
    self.sendMove ("seller", {"x": [
      {
        "b": self.buildingId,
        "i": "foo",
        "n": 2,
        "ap": 100,
      },
      {
        "b": self.buildingId,
        "i": "foo",
        "n": 2,
        "ap": 50,
      },
    ]})
    # Make sure the two name updates are confirmed in the intended order
    # relative to each other (otherwise the IDs get messed up).
    self.generate (1)
    self.sendMove ("buyer", {"x": [
      {
        "b": self.buildingId,
        "i": "foo",
        "n": 1,
        "bp": 10,
      },
      {
        "b": self.buildingId,
        "i": "foo",
        "n": 1,
        "bp": 20,
      },
    ]})
    self.generate (1)
    self.expectBalances ({
      "buyer": (970, 30),
      "seller": 0,
      "building": 0,
    })
    self.expectItems (self.buildingId, "buyer", {"foo": 0})
    self.expectItems (self.buildingId, "seller", {"foo": (6, 4)})
    self.assertEqual (self.getBuildings ()[self.buildingId].getOrderbook (), {
      "foo":
        {
          "item": "foo",
          "bids":
            [
              {
                "id": firstOrderId + 3,
                "account": "buyer",
                "price": 20,
                "quantity": 1,
              },
              {
                "id": firstOrderId + 2,
                "account": "buyer",
                "price": 10,
                "quantity": 1,
              },
            ],
          "asks":
            [
              {
                "id": firstOrderId + 1,
                "account": "seller",
                "price": 50,
                "quantity": 2,
              },
              {
                "id": firstOrderId + 0,
                "account": "seller",
                "price": 100,
                "quantity": 2,
              },
            ],
        },
    })

    self.mainLogger.info ("Cancelling orders...")
    self.sendMove ("buyer", {"x": [{"c": firstOrderId + 3}]})
    self.sendMove ("seller", {"x": [{"c": firstOrderId + 1}]})
    self.generate (1)
    self.expectBalances ({
      "buyer": (990, 10),
      "seller": 0,
      "building": 0,
    })
    self.expectItems (self.buildingId, "buyer", {"foo": 0})
    self.expectItems (self.buildingId, "seller", {"foo": (8, 2)})
    self.assertEqual (self.getBuildings ()[self.buildingId].getOrderbook (), {
      "foo":
        {
          "item": "foo",
          "bids":
            [
              {
                "id": firstOrderId + 2,
                "account": "buyer",
                "price": 10,
                "quantity": 1,
              },
            ],
          "asks":
            [
              {
                "id": firstOrderId + 0,
                "account": "seller",
                "price": 100,
                "quantity": 2,
              },
            ],
        },
    })

    self.mainLogger.info ("Partially filling orders...")
    self.sendMove ("seller", {"x": [
      {
        "b": self.buildingId,
        "i": "foo",
        "n": 2,
        "ap": 0,
      },
    ]})
    self.generate (1)
    blk1 = self.rpc.xaya.getblockheader (self.rpc.xaya.getbestblockhash ())
    self.sendMove ("buyer", {"x": [
      {
        "b": self.buildingId,
        "i": "foo",
        "n": 4,
        "bp": 200,
      },
    ]})
    self.generate (1)
    blk2 = self.rpc.xaya.getblockheader (self.rpc.xaya.getbestblockhash ())
    self.expectBalances ({
      "buyer": (590, 200),
      "seller": 210 - 2 * 21,
      "building": 21,
    })
    self.expectItems (self.buildingId, "buyer", {"foo": 4})
    self.expectItems (self.buildingId, "seller", {"foo": 6})
    self.assertEqual (self.getBuildings ()[self.buildingId].getOrderbook (), {
      "foo":
        {
          "item": "foo",
          "bids":
            [
              {
                "id": firstOrderId + 5,
                "account": "buyer",
                "price": 200,
                "quantity": 1,
              },
            ],
          # The remaining ask at zero was filled with the subsequent
          # buy order.
          "asks": [],
        },
    })

    self.mainLogger.info ("Checking trade history...")
    self.assertEqual (self.getRpc ("gettradehistory",
                                   item="foo", building=self.buildingId), [
      {
        "height": blk1["height"],
        "timestamp": blk1["time"],
        "buildingid": self.buildingId,
        "item": "foo",
        "price": 10,
        "quantity": 1,
        "cost": 10,
        "seller": "seller",
        "buyer": "buyer",
      },
      {
        "height": blk2["height"],
        "timestamp": blk2["time"],
        "buildingid": self.buildingId,
        "item": "foo",
        "price": 0,
        "quantity": 1,
        "cost": 0,
        "seller": "seller",
        "buyer": "buyer",
      },
      {
        "height": blk2["height"],
        "timestamp": blk2["time"],
        "buildingid": self.buildingId,
        "item": "foo",
        "price": 100,
        "quantity": 2,
        "cost": 200,
        "seller": "seller",
        "buyer": "buyer",
      },
    ])

    self.testReorg (reorgBlk)

  def testReorg (self, blk):
    self.mainLogger.info ("Testing reorg...")

    originalState = self.getGameState ()
    originalHistory = self.getRpc ("gettradehistory",
                                   item="foo", building=self.buildingId)
    self.rpc.xaya.invalidateblock (blk)

    self.expectBalances ({
      "buyer": 1_000,
      "seller": 0,
      "building": 0,
      "gifted": 0,
    })
    self.expectItems (self.buildingId, "buyer", {"foo": 0, "bar": 0})
    self.expectItems (self.buildingId, "seller", {"foo": 10, "bar": 20})
    self.expectItems (self.buildingId, "gifted", {"foo": 0, "bar": 0})
    self.assertEqual (self.getBuildings ()[self.buildingId].getOrderbook (), {})
    self.assertEqual (self.getRpc ("gettradehistory",
                                   item="foo", building=self.buildingId),
                      [])

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)
    self.assertEqual (self.getRpc ("gettradehistory",
                                   item="foo", building=self.buildingId),
                      originalHistory)


if __name__ == "__main__":
  DexTest ().main ()
