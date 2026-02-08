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
Tests coin operations (transfers / burns) and account balances.
"""

from pxtest import PXTest


class CoinOpsTest (PXTest):

  def expectBalances (self, expected):
    """
    Verifies that a set of accounts has given balances, as stated
    in the dictionary passed in.
    """

    acc = self.getAccounts ()
    for k, v in expected.items ():
      if k not in acc:
        self.assertEqual (v, 0)
      else:
        self.assertEqual (acc[k].getBalance (), v)

  def run (self):
    self.mainLogger.info ("Setting up test situation...")
    # No need to initialise accounts.  Coin operations also work
    # without a chosen faction.  But we need to register the name or else
    # the move sending gets confused by the reorg somehow.
    self.env.register ("p", "domob")
    self.giftCoins ({"domob": 100})

    self.generate (1)
    self.snapshot = self.env.snapshot ()

    self.mainLogger.info ("Coin transfers and burns...")
    self.sendMove ("domob", {"vc": {"b": 10, "t": {"andy": 2, "domob": 10}}})
    self.generate (1)

    self.expectBalances ({
      "domob": 88,
      "andy": 2,
      "daniel": 0,
    })

    self.testReorg ()

  def testReorg (self):
    self.mainLogger.info ("Testing reorg...")

    self.snapshot.restore ()

    self.sendMove ("domob", {"vc": {"t": {"daniel": 100}}})
    self.generate (1)

    self.expectBalances ({
      "domob": 0,
      "andy": 0,
      "daniel": 100,
    })


if __name__ == "__main__":
  CoinOpsTest ().main ()
