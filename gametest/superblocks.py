#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2026  Autonomous Worlds Ltd
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
Tests handling of superblocks in the GSP.
"""

from pxtest import PXTest

from movement import MovementTest


class SuperblocksTest (MovementTest):

  def run (self):
    self.initAccount ("domob", "g")
    self.createCharacters ("domob")
    self.generate (1)
    self.changeCharacterVehicle ("domob", "light attacker")

    # Start off from a known good location to make sure all is fine and
    # not flaky depending on the randomised spawn position.
    self.offset = {"x": -1377, "y": 1263}
    self.moveCharactersTo ({"domob": self.offset})

    # We start at a superblock.
    self.generate (1, superblocks=True)
    startHeight = self.getSuperblockHeight ()

    # We start movement with speed one hex/superblock,
    # and mine it into a non-superblock.  That will confirm the move,
    # but not start movement.
    self.setWaypoints ("domob", [{"x": 100, "y": 0}], speed=1_000)
    self.generate (1, superblocks=False)
    self.assertEqual (self.getSuperblockHeight (), startHeight)
    pos, mv = self.getMovement ("domob")
    assert mv is not None, "waypoints were not confirmed"
    self.assertEqual (pos, {"x": 0, "y": 0})

    # Another non-superblock, which should not move the unit.
    self.generate (1, superblocks=False)
    self.assertEqual (self.getSuperblockHeight (), startHeight)
    self.expectPosition ("domob", {"x": 0, "y": 0})

    # Now we mine a superblock, which should advance the unit.
    self.generate (1, superblocks=True)
    self.assertEqual (self.getSuperblockHeight (), startHeight + 1)
    self.expectPosition ("domob", {"x": 1, "y": 0})

    # And two more.
    self.generate (2, superblocks=True)
    self.assertEqual (self.getSuperblockHeight (), startHeight + 3)
    self.expectPosition ("domob", {"x": 3, "y": 0})

    # Non-superblock does nothing again.
    self.generate (1, superblocks=False)
    self.assertEqual (self.getSuperblockHeight (), startHeight + 3)
    self.expectPosition ("domob", {"x": 3, "y": 0})


if __name__ == "__main__":
  SuperblocksTest ().main ()
