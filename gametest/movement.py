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
    self.testWaypointExtension ()
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

  def testWaypointExtension (self):
    """
    Tests how we can use waypoint extension to move a couple of units
    along a shared path with just an initial specific segment.
    """

    self.mainLogger.info ("Testing convoy movement through wpx...")

    # Set up three test characters and a shared initial waypoint.  The
    # characters need custom paths to go to the initial waypoint.
    self.createCharacters ("domob", 2)
    self.generate (1)
    initialWp = self.offset
    self.moveCharactersTo ({
      "domob": offsetCoord ({"x": 10, "y": -3}, initialWp, False),
      "domob 2": offsetCoord ({"x": 5, "y": 2}, initialWp, False),
      "domob 3": offsetCoord ({"x": -1, "y": 8}, initialWp, False),
    })

    # Build up a single move that sends them to a target far away,
    # but sharing most of the path among them.
    target = offsetCoord ({"x": -1_234, "y": 570}, self.offset, False)
    ops = []
    ids = []
    for nm in ["domob", "domob 2", "domob 3"]:
      c = self.getCharacters ()[nm]
      ids.append (c.getId ())
      ops.append ({
        "id": c.getId (),
        "wp": c.findPath (initialWp),
      })
    path = self.rpc.game.findpath (source=initialWp, target=target,
                                   faction="r", l1range=2_000, exbuildings=[])
    ops.append ({
      "id": ids,
      "wpx": path["encoded"],
    })
    self.sendMove ("domob", {"c": ops})

    # Let them move there and check the expected outcome (they arrive
    # all there, just blocked up against each other).
    self.generate (500)
    chars = self.getCharacters ()
    self.assertEqual (chars["domob 2"].getPosition (), target)
    self.assertEqual (chars["domob 3"].getPosition (),
                      offsetCoord ({"x": 1, "y": -1}, target, False))
    self.assertEqual (chars["domob"].getPosition (),
                      offsetCoord ({"x": 2, "y": -2}, target, False))

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
