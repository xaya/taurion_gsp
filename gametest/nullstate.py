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
Tests that getnullstate works (very roughly).
"""

from pxtest import PXTest, offsetCoord


class NullstateTest (PXTest):

  def run (self):
    self.generate (10)

    self.assertEqual (self.getCustomState ("data", "getnullstate"), None)

    res = self.rpc.game.getnullstate ()
    self.assertEqual (res["state"], "up-to-date")
    self.assertEqual (res["chain"], "regtest")
    self.assertEqual (res["height"], self.rpc.xaya.getblockcount ())
    self.assertEqual (res["blockhash"], self.rpc.xaya.getbestblockhash ())


if __name__ == "__main__":
  NullstateTest ().main ()
