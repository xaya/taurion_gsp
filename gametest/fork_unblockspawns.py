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
Integration test the "unblock spawns" fork.
"""

from pxtest import PXTest

FORK_HEIGHT = 500


class UnblockSpawnsForkTest (PXTest):

  def run (self):
    self.collectPremine ()
    self.splitPremine ()

    self.initAccount ("red", "r")
    self.initAccount ("green", "g")
    self.initAccount ("blue", "b")
    self.generate (1)

    # Remember a block before the fork is active, which we will later
    # reorg to and check that the fork is then still inactive.
    self.generate (100)
    reorgBlock = self.rpc.xaya.getbestblockhash ()

    # Advance until almost the fork height.
    self.advanceToHeight (FORK_HEIGHT - 2)

    self.mainLogger.info ("Creating characters before the fork...")
    for nm in ["red", "green", "blue"]:
      self.createCharacters (nm)
    self.generate (1)
    chars = self.getCharacters ()
    for nm in ["red", "green", "blue"]:
      self.assertEqual (chars[nm].isInBuilding (), False)

    self.mainLogger.info ("Creating characters after the fork...")
    for nm in ["red", "green", "blue"]:
      self.createCharacters (nm)
    self.generate (1)
    self.assertEqual (self.rpc.xaya.getblockcount (), FORK_HEIGHT)
    chars = self.getCharacters ()
    buildings = self.getBuildings ()
    for nm, t in [("red", "r ss"), ("green", "g ss"), ("blue", "b ss")]:
      c = chars["%s 2" % nm]
      self.assertEqual (c.isInBuilding (), True)
      self.assertEqual (buildings[c.getBuildingId ()].getType (), t)

    # Test a reorg back to before the fork.
    self.mainLogger.info ("Testing reorg to before the fork...")
    oldState = self.getGameState ()

    self.rpc.xaya.invalidateblock (reorgBlock)
    self.initAccount ("reorg", "r")
    self.generate (1)
    self.createCharacters ("reorg")
    self.generate (1)
    self.assertEqual (self.getCharacters ()["reorg"].isInBuilding (), False)

    self.rpc.xaya.reconsiderblock (reorgBlock)
    self.expectGameState (oldState)


if __name__ == "__main__":
  UnblockSpawnsForkTest ().main ()
