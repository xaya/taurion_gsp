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
Tests the "game start" fork:  Before, the burnsale and coin operations
should already be working.  After, the game itself starts (e.g. choosing
factions, creating characters).
"""

from pxtest import PXTest


class ForkGameStartTest (PXTest):

  def run (self):
    self.recreateGameDaemon (extraArgs=["-fork_height_gamestart=100"])

    self.sendMove ("domob", {
      "vc": {"m": {}, "b": 10, "t": {"daniel": 100}}
    }, burn=1)
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)

    _, height = self.env.getChainTip ()
    assert height < 100

    accounts = self.getAccounts ()
    self.assertEqual (accounts["domob"].getBalance (), 10_000 - 10 - 100)
    self.assertEqual (accounts["daniel"].getBalance (), 100)
    self.assertEqual (accounts["domob"].getFaction (), None)
    self.assertEqual (self.getCharacters (), {})

    self.advanceToHeight (99)
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)

    self.assertEqual (self.getAccounts ()["domob"].getFaction (), "r")
    chars = self.getCharacters ()
    self.assertEqual (len (chars), 1)
    assert "domob" in chars


if __name__ == "__main__":
  ForkGameStartTest ().main ()
