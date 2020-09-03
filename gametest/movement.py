#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2020  Autonomous Worlds Ltd
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
Tests movement of characters on the map.
"""

from pxtest import PXTest, offsetCoord


class MovementTest (PXTest):

  def setWaypoints (self, owner, wp, speed=None):
    """
    Sends a move to update the waypoints of the character with the given owner.
    """

    c = self.getCharacters ()[owner]
    offset = [offsetCoord (p, self.offset, False) for p in wp]

    encoded = self.rpc.game.encodewaypoints (wp=offset)

    mv = {"wp": encoded}
    if speed:
      mv["speed"] = speed

    return c.sendMove (mv)

  def moveTowards (self, owner, target):
    """
    Moves the character of the given owner towards the given target (offset
    by self.offset), using findpath rather than direct waypoints.
    """

    c = self.getCharacters ()[owner]
    return c.moveTowards (offsetCoord (target, self.offset, False))

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

  def expectPosition (self, owner, expected):
    """
    Expects the position of the given character to be the given value.
    """

    pos, _ = self.getMovement (owner)
    self.assertEqual (pos, expected)

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
    self.initAccount ("domob", "g")
    self.createCharacters ("domob")
    self.generate (1)
    self.changeCharacterVehicle ("domob", "light attacker")

    # Start off from a known good location to make sure all is fine and
    # not flaky depending on the randomised spawn position.
    self.offset = {"x": -1377, "y": 1263}
    self.moveCharactersTo ({"domob": self.offset})

    self.mainLogger.info ("Setting basic path for character...")
    wp = [
      {"x": 9, "y": 0},
      {"x": 6, "y": 3},
      {"x": 6, "y": 3},
      {"x": 0, "y": 3},
      {"x": 0, "y": 0},
      {"x": -3, "y": 0},
      {"x": -9, "y": 6},
      {"x": -9, "y": 6},
    ]
    self.setWaypoints ("domob", wp)
    self.generate (1)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (mv["partialstep"], 0)
    self.assertEqual (pos, {"x": 3, "y": 0})
    self.reorgBlock = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Finishing the movement...")
    self.expectMovement ("domob", wp)

    self.mainLogger.info ("Testing empty waypoints...")
    self.moveTowards ("domob", {"x": 5, "y": 7})
    self.generate (1)
    correctPos, mv = self.getMovement ("domob")
    assert mv is not None
    self.setWaypoints ("domob", [])
    self.generate (1)
    pos, mv = self.getMovement ("domob")
    self.assertEqual ((pos, mv), (correctPos, None))

    self.mainLogger.info ("Testing path with invalid waypoints...")
    wp = [
      {"x": 0, "y": 0},
      {"x": 200, "y": 0},
      {"x": 201, "y": 1},
    ]
    self.setWaypoints ("domob", wp)
    self.generate (300)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 200, "y": 0})
    assert mv is None

    self.testChosenSpeed ()
    self.testBlockingBuilding ()
    self.testConvoy ()
    self.testReorg ()

  def testChosenSpeed (self):
    self.mainLogger.info ("Testing chosen speed...")

    self.moveCharactersTo ({
      "domob": offsetCoord ({"x": 0, "y": 0}, self.offset, False),
    })

    # Move the character with reduced speed.
    self.setWaypoints ("domob", [{"x": 100, "y": 0}], speed=1000)
    self.generate (10)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 10, "y": 0})
    self.assertEqual (mv["chosenspeed"], 1000)

    # Adjust the speed to be higher than the natural speed of 2'000,
    # and expect movement with the natural speed.
    self.getCharacters ()["domob"].sendMove ({"speed": 10000})
    self.generate (10)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 40, "y": 0})
    self.assertEqual (mv["chosenspeed"], 10000)

    # Sending another movement in-between without speed will revert it to
    # the default one.
    self.setWaypoints ("domob", [{"x": 100, "y": 0}], speed=1000)
    self.generate (10)
    pos, _ = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 50, "y": 0})
    self.setWaypoints ("domob", [{"x": 0, "y": 0}])
    self.generate (10)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 20, "y": 0})
    assert "chosenspeed" not in mv

    # Letting the movement finish and then sending a new movement will also
    # revert to intrinsic speed.
    self.setWaypoints ("domob", [{"x": 100, "y": 0}], speed=1000)
    self.generate (10)
    pos, _ = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 30, "y": 0})
    self.generate (100)
    pos, _ = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 100, "y": 0})
    self.setWaypoints ("domob", [{"x": 0, "y": 0}])
    self.generate (10)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 70, "y": 0})
    assert "chosenspeed" not in mv

    # Stop the character to avoid confusing later tests.
    self.setWaypoints ("domob", [])
    self.generate (1)

  def testBlockingBuilding (self):
    """
    Tests how a new building can block the movement when it is placed
    into the path.
    """

    self.mainLogger.info ("Testing blocking the path...")

    self.moveCharactersTo ({
      "domob": offsetCoord ({"x": 50, "y": 0}, self.offset, False),
    })
    self.build ("huesli", None,
                offsetCoord ({"x": 30, "y": 0}, self.offset, False),
                rot=0)

    # Set waypoints across a blocked path.
    self.setWaypoints ("domob", [{"x": 0, "y": 0}])
    self.generate (10)

    # The character should be blocked by the obstacle.
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 31, "y": 0})
    assert mv["blockedturns"] > 0

    # Let movement stop completely.
    self.generate (10 + self.roConfig ().params.blocked_step_retries)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 31, "y": 0})
    self.assertEqual (mv, None)

    # Move around the obstacle.
    self.setWaypoints ("domob", [
      {"x": 31, "y": 1},
      {"x": 0, "y": 1},
      {"x": 0, "y": 0},
    ])
    self.generate (100)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (pos, {"x": 0, "y": 0})
    self.assertEqual (mv, None)

  def testConvoy (self):
    """
    Tests movement of multiple characters in a "convoy", with semantics
    after the unblock-spawns fork.
    """

    self.mainLogger.info ("Testing convoy movement...")

    # We should already be beyond the fork height, but check that.
    assert self.rpc.xaya.getblockcount () > 500

    # Set up three test characters around a common centre, and then
    # send them to move with the same path.  They will "collidate" on the
    # initial step, but due to the slowdown on entering a coordinate with
    # another vehicle on it, should just split out into a convoy over time.
    self.createCharacters ("domob", 2)
    self.generate (1)
    self.moveCharactersTo ({
      "domob": offsetCoord ({"x": 1, "y": -1}, self.offset, False),
      "domob 2": offsetCoord ({"x": 1, "y": 0}, self.offset, False),
      "domob 3": offsetCoord ({"x": 0, "y": 1}, self.offset, False),
    })

    wp = [{"x": 0, "y": 0}, {"x": -10, "y": 0}]
    self.setWaypoints ("domob 3", wp, speed=1000)
    self.setWaypoints ("domob 2", wp, speed=1000)
    self.setWaypoints ("domob", wp, speed=1000)

    self.generate (1)
    self.expectPosition ("domob", {"x": 0, "y": 0})
    self.expectPosition ("domob 2", {"x": 1, "y": 0})
    self.expectPosition ("domob 3", {"x": 0, "y": 1})

    self.generate (1)
    self.expectPosition ("domob", {"x": -1, "y": 0})
    self.expectPosition ("domob 2", {"x": 0, "y": 0})
    self.expectPosition ("domob 3", {"x": 0, "y": 1})

    self.generate (1)
    self.expectPosition ("domob", {"x": -2, "y": 0})
    self.expectPosition ("domob 2", {"x": -1, "y": 0})
    self.expectPosition ("domob 3", {"x": 0, "y": 0})

    self.generate (5)
    self.expectPosition ("domob", {"x": -7, "y": 0})
    self.expectPosition ("domob 2", {"x": -6, "y": 0})
    self.expectPosition ("domob 3", {"x": -5, "y": 0})

    # Let them move onto the target tile and collect up there together.
    # Then move back off, which should again be as a convoy.
    self.generate (20)
    self.expectPosition ("domob", {"x": -10, "y": 0})
    self.expectPosition ("domob 2", {"x": -10, "y": 0})
    self.expectPosition ("domob 3", {"x": -10, "y": 0})

    wp = [{"x": 0, "y": 0}]
    self.setWaypoints ("domob 3", wp, speed=1000)
    self.setWaypoints ("domob 2", wp, speed=1000)
    self.setWaypoints ("domob", wp, speed=1000)

    self.generate (1)
    self.expectPosition ("domob", {"x": -9, "y": 0})
    self.expectPosition ("domob 2", {"x": -10, "y": 0})
    self.expectPosition ("domob 3", {"x": -10, "y": 0})

    self.generate (1)
    self.expectPosition ("domob", {"x": -8, "y": 0})
    self.expectPosition ("domob 2", {"x": -9, "y": 0})
    self.expectPosition ("domob 3", {"x": -10, "y": 0})

    self.generate (1)
    self.expectPosition ("domob", {"x": -7, "y": 0})
    self.expectPosition ("domob 2", {"x": -8, "y": 0})
    self.expectPosition ("domob 3", {"x": -9, "y": 0})

    self.generate (7)
    self.expectPosition ("domob", {"x": 0, "y": 0})
    self.expectPosition ("domob 2", {"x": -1, "y": 0})
    self.expectPosition ("domob 3", {"x": -2, "y": 0})

    self.generate (20)
    self.expectPosition ("domob", {"x": 0, "y": 0})
    self.expectPosition ("domob 2", {"x": 0, "y": 0})
    self.expectPosition ("domob 3", {"x": 0, "y": 0})

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
      {"x": -5, "y": 5},
    ]
    self.setWaypoints ("domob", wp)
    self.expectMovement ("domob", wp)

    self.rpc.xaya.reconsiderblock (self.reorgBlock)
    self.expectGameState (originalState)



if __name__ == "__main__":
  MovementTest ().main ()
