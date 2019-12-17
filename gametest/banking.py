#!/usr/bin/env python

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019  Autonomous Worlds Ltd
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
Tests banking of resources / items.
"""

from pxtest import PXTest


class BankingTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.bankPos = {"x": -175, "y": 810}
    self.noBankPos = {"x": -176, "y": 810}

    self.mainLogger.info ("Creating test character...")
    self.dropLoot (self.noBankPos, {"gold prize": 1, "silver prize": 2})
    self.initAccount ("domob", "g")
    self.createCharacters ("domob")
    self.generate (1)
    self.moveCharactersTo ({
      "domob": self.noBankPos,
    })
    self.getCharacters ()["domob"].sendMove ({
      "pu":
        {
          "f":
            {
              "gold prize": 10,
              "silver prize": 10,
            }
        }
    })
    self.generate (1)
    self.assertEqual (self.getCharacters ()["domob"].getFungibleInventory (), {
      "gold prize": 1,
      "silver prize": 2,
    })
    self.assertEqual (self.getAccounts ()["domob"].getFungibleBanked (), {})

    # Generate a block we will invalidate later.  Also make sure that we have
    # some blocks to make this chain longest.
    self.generate (1)
    self.reorgBlock = self.rpc.xaya.getbestblockhash ()
    self.generate (10)

    self.mainLogger.info ("Banking prizes by movement...")
    self.getCharacters ()["domob"].sendMove ({"wp": [self.bankPos]})
    self.generate (1)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.getPosition (), self.bankPos)
    self.assertEqual (c.getFungibleInventory (), {})
    self.assertEqual (self.getAccounts ()["domob"].getFungibleBanked (), {
      "gold prize": 1,
      "silver prize": 2,
    })

    self.testResourceSets ()
    self.testReorg ()

  def testResourceSets (self):
    self.mainLogger.info ("Banking resources for sets...")
    self.dropLoot (self.bankPos, {
      "raw a": 25,
      "raw b": 20,
      "raw c": 20,
      "raw d": 20,
      "raw e": 20,
      "raw f": 20,
      "raw g": 20,
      "raw h": 20,
      "raw i": 20,
    })
    self.generate (1)

    while self.getRpc ("getgroundloot"):
      c = self.getCharacters ()["domob"]
      c.sendMove ({
        "pu":
          {
            "f":
              {
                "raw a": 1000,
                "raw b": 1000,
                "raw c": 1000,
                "raw d": 1000,
                "raw e": 1000,
                "raw f": 1000,
                "raw g": 1000,
                "raw h": 1000,
                "raw i": 1000,
              }
          }
      })
      self.generate (1)

    c = self.getCharacters ()["domob"]
    self.assertEqual (c.getFungibleInventory (), {})

    a = self.getAccounts ()["domob"]
    self.assertEqual (a.data["bankingpoints"], 1)
    self.assertEqual (a.getFungibleBanked (), {
      "gold prize": 1,
      "silver prize": 2,
      "raw a": 5,
    })

  def testReorg (self):
    self.mainLogger.info ("Testing reorg...")

    oldState = self.getGameState ()

    # Invalidate the first banking and leave the character out of
    # the banking area instead.
    self.rpc.xaya.invalidateblock (self.reorgBlock)
    self.assertEqual (self.rpc.xaya.name_pending ("domob"), [])
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.isMoving (), False)
    self.assertEqual (c.getPosition (), self.noBankPos)
    self.generate (10)

    a = self.getAccounts ()["domob"]
    self.assertEqual (a.getFungibleBanked (), {})
    self.assertEqual (a.data["bankingpoints"], 0)

    # Reconsider the previous chain to restore the state.
    self.rpc.xaya.reconsiderblock (self.reorgBlock)
    self.expectGameState (oldState)
    assert len (self.getAccounts ()["domob"].getFungibleBanked ()) > 0


if __name__ == "__main__":
  BankingTest ().main ()
