#!/usr/bin/env python

from pxtest import PXTest

"""
Tests movement of characters on the map.
"""


class MovementTest (PXTest):

  def setWaypoints (self, character, wp):
    """
    Sends a move to update the waypoints of the character with the given name.
    """

    c = self.getCharacters ()[character]
    return c.sendMove ({"wp": wp})

  def getMovement (self, character):
    """
    Retrieves the movement structure of the given character from the current
    game state (and None if the character is not moving).  Returns a pair
    of (position, movement).
    """

    c = self.getCharacters ()[character]

    if c.isMoving ():
      return c.getPosition (), c.data["movement"]

    return c.getPosition (), None

  def expectMovement (self, character, wp):
    """
    Expects that the given character moves along the set of waypoints
    specified.  Generates blocks and verifies that.
    """

    finalWp = wp[-1]

    lastPos, _ = self.getMovement (character)
    nonMoves = 0
    blocks = 0

    while len (wp) > 0:
      # Make sure to break out of the loop if something is wrong and
      # we are not actually progressing
      assert blocks < 100

      self.generate (1)
      blocks += 1
      pos, _ = self.getMovement (character)

      if pos == lastPos:
        nonMoves += 1
      lastPos = pos

      while len (wp) > 0 and pos == wp[0]:
        wp = wp[1:]

    self.log.info ("Moved for %d blocks, %d the character didn't advance"
        % (blocks, nonMoves))

    # In the end, we should have stopped at the final position.
    pos, mv = self.getMovement (character)
    self.assertEqual (pos, finalWp)
    assert mv is None

  def run (self):
    self.generate (101);

    self.mainLogger.info ("Creating test character...")
    self.createCharacter ("domob", "foo", "r")
    self.generate (1)

    self.mainLogger.info ("Setting basic path for character...")
    wp = [
      {"x": 5, "y": -2},
      {"x": 3, "y": 3},
      {"x": 3, "y": 3},
      {"x": 0, "y": 0},
      {"x": -2, "y": -2},
      {"x": -2, "y": -2},
    ]
    self.setWaypoints ("foo", wp)
    self.generate (1)
    pos, mv = self.getMovement ("foo")
    self.assertEqual (mv["partialstep"], 750)
    self.assertEqual (pos, {"x": 0, "y": 0})

    self.mainLogger.info ("Finishing the movement...")
    self.expectMovement ("foo", wp)

    self.mainLogger.info ("Testing path with too far waypoints...")
    wp = [
      {"x": 0, "y": 0},
      {"x": 99, "y": 1},
      {"x": 0, "y": -1},
    ]
    self.setWaypoints ("foo", wp)
    self.generate (200)
    pos, mv = self.getMovement ("foo")
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

    blk = self.rpc.xaya.getblockhash (105)
    self.rpc.xaya.invalidateblock (blk)

    wp = [
      {"x": -1, "y": 5},
    ]
    self.setWaypoints ("foo", wp)
    self.expectMovement ("foo", wp)

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)



if __name__ == "__main__":
  MovementTest ().main ()
