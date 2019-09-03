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

from pxtest import PXTest, offsetCoord

"""
Tests movement of characters on the map.
"""


class MovementTest (PXTest):

  def setWaypoints (self, owner, wp):
    """
    Sends a move to update the waypoints of the character with the given owner.
    """

    wpOffs = [offsetCoord (p, self.offset, False) for p in wp]

    c = self.getCharacters ()[owner]
    return c.sendMove ({"wp": wpOffs})

  def getMovement (self, owner):
    """
    Retrieves the movement structure of the given character from the current
    game state (and None if the character is not moving).  Returns a pair
    of (position, movement).
    """

    c = self.getCharacters ()[owner]
    pos = offsetCoord (c.getPosition (), self.offset, True)

    if c.isMoving ():
      return pos, c.data["movement"]

    return pos, None

  def expectMovement (self, owner, wp):
    """
    Expects that the given character moves along the set of waypoints
    specified.  Generates blocks and verifies that.
    """

    finalWp = wp[-1]

    lastPos, _ = self.getMovement (owner)
    nonMoves = 0
    blocks = 0

    while len (wp) > 0:
      # Make sure to break out of the loop if something is wrong and
      # we are not actually progressing
      assert blocks < 100

      self.generate (1)
      blocks += 1
      pos, _ = self.getMovement (owner)

      if pos == lastPos:
        nonMoves += 1
      lastPos = pos

      while len (wp) > 0 and pos == wp[0]:
        wp = wp[1:]

    self.log.info ("Moved for %d blocks, %d the character didn't advance"
        % (blocks, nonMoves))

    # In the end, we should have stopped at the final position.
    pos, mv = self.getMovement (owner)
    self.assertEqual (pos, finalWp)
    assert mv is None

  def run (self):
    self.collectPremine ()

    self.mainLogger.info ("Creating test character...")
    self.createCharacter ("domob", "r")
    self.generate (1)

    # Start off from a known good location to make sure all is fine and
    # not flaky depending on the randomised spawn position.
    self.offset = {"x": -1377, "y": 1263}
    self.moveCharactersTo ({"domob": self.offset})

    self.mainLogger.info ("Setting basic path for character...")
    wp = [
      {"x": 12, "y": 0},
      {"x": 3, "y": 3},
      {"x": 3, "y": 3},
      {"x": 0, "y": 0},
      {"x": -9, "y": -6},
      {"x": -9, "y": -6},
    ]
    self.setWaypoints ("domob", wp)
    self.generate (1)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (mv["partialstep"], 0)
    self.assertEqual (pos, {"x": 3, "y": 0})
    self.reorgBlock = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Finishing the movement...")
    self.expectMovement ("domob", wp)

    self.mainLogger.info ("Testing path with too far waypoints...")
    wp = [
      {"x": 0, "y": 0},
      {"x": 99, "y": 1},
      {"x": 0, "y": -1},
    ]
    self.setWaypoints ("domob", wp)
    self.generate (200)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 99, "y": 1})
    assert mv is None

    self.testBlockingVehicle ()
    self.testReorg ()

  def testBlockingVehicle (self):
    """
    Tests how another vehicle can block the movement when it is placed
    into the path during the "stepping" phase.
    """

    self.mainLogger.info ("Testing blocking the path...")

    self.createCharacter ("blocker", "r")
    self.generate (1)

    self.moveCharactersTo ({
      "domob": offsetCoord ({"x": 70, "y": 0}, self.offset, False),
      "blocker": offsetCoord ({"x": 50, "y": 1}, self.offset, False),
    })

    # Calculate the steps and then block the path.
    self.setWaypoints ("domob", [{"x": 0, "y": 0}])
    self.generate (3)
    self.setWaypoints ("blocker", [{"x": 50, "y": 0}])
    self.generate (5)

    # The character should be blocked by the obstacle.
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 51, "y": 0})
    assert mv["blockedturns"] > 0

    # Move the obstacle away and let the character continue moving.
    self.setWaypoints ("blocker", [{"x": 50, "y": 1}])
    self.generate (5)
    pos, mv = self.getMovement ("domob")
    assert pos["x"] < 50
    assert "blockedturns" not in mv

    # Block the path again and let movement stop completely.
    self.moveCharactersTo ({
      "blocker": offsetCoord ({"x": 30, "y": 0}, self.offset, False),
    })
    self.generate (20)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 31, "y": 0})
    self.assertEqual (mv, None)

    # Doing another path finding should work around it.
    self.setWaypoints ("domob", [{"x": 0, "y": 0}])
    self.generate (100)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 0, "y": 0})
    self.assertEqual (mv, None)

  def testReorg (self):
    """
    Reorgs away all the current chain, builds up a small alternate chain
    and then reorgs back to the original chain.  Verifies that the game state
    stays the same.
    """

    self.mainLogger.info ("Testing a reorg...")
    originalState = self.getGameState ()

    self.rpc.xaya.invalidateblock (self.reorgBlock)

    wp = [
      {"x": -1, "y": 5},
    ]
    self.setWaypoints ("domob", wp)
    self.expectMovement ("domob", wp)

    self.rpc.xaya.reconsiderblock (self.reorgBlock)
    self.expectGameState (originalState)



if __name__ == "__main__":
  MovementTest ().main ()
