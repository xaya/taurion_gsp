#!/usr/bin/env python

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
    self.generate (101);

    self.mainLogger.info ("Creating test character...")
    self.createCharacter ("domob", "r")
    self.generate (1)

    # Start off from a known good location to make sure all is fine and
    # not flaky depending on the randomised spawn position.
    self.offset = {"x": -1100, "y": 1042}
    self.moveCharactersTo ({"domob": self.offset})

    self.mainLogger.info ("Setting basic path for character...")
    wp = [
      {"x": 5, "y": -2},
      {"x": 3, "y": 3},
      {"x": 3, "y": 3},
      {"x": 0, "y": 0},
      {"x": -2, "y": -2},
      {"x": -2, "y": -2},
    ]
    self.setWaypoints ("domob", wp)
    self.generate (1)
    pos, mv = self.getMovement ("domob")
    self.assertEqual (mv["partialstep"], 750)
    self.assertEqual (pos, {"x": 0, "y": 0})
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

    # TODO: Once we have buildings, test the situation of an obstacle
    # appearing dynamically on the next steps or later in the waypoint list.

    self.testReorg ()

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
