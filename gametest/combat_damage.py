#!/usr/bin/env python

from pxtest import PXTest, offsetCoord

"""
Tests dealing damage, regenerating the shield and killing characters.
"""

# Owner of the target character.
TARGET = "target"


class CombatDamageTest (PXTest):

  def getTargetHP (self):
    """
    Returns the current and max HP struct for the given character name or None
    if the character does not exist.
    """

    chars = self.getCharacters ()

    if TARGET not in chars:
      return None, None

    hp = chars[TARGET].data["combat"]["hp"]
    return hp["current"], hp["max"]

  def setTargetWP (self, wp):
    """
    Sets the waypoint to which the target character should move.
    """

    wp = offsetCoord (wp, self.offset, False)

    c = self.getCharacters ()[TARGET]
    c.sendMove ({"wp": [wp]})

  def run (self):
    self.generate (110);

    numAttackers = 5

    self.mainLogger.info ("Creating test characters...")
    for i in range (numAttackers):
      self.createCharacter ("name %d" % i, "r")
    self.createCharacter (TARGET, "b")
    self.generate (1)

    # We use one of the attackers as offset and move others there (to ensure
    # that all of them are in a single spot).
    c = self.getCharacters ()["name 0"]
    self.offset = c.getPosition ()
    charTargets = {}
    for i in range (1, numAttackers):
      charTargets["name %d" % i] = self.offset
    self.moveCharactersTo (charTargets)

    startPos = offsetCoord ({"x": 0, "y": 5}, self.offset, False)
    self.moveCharactersTo ({TARGET: startPos})
    self.getTargetHP ()

    self.mainLogger.info ("Taking some damage...")
    self.setTargetWP ({"x": 0, "y": 11})
    self.generate (10)
    hp, maxHP = self.getTargetHP ()
    assert (hp is not None) and (maxHP is not None)
    assert hp["armour"] < maxHP["armour"]
    assert hp["shield"] < maxHP["shield"]

    self.restoreBlock = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Regenerating shield...")
    self.generate (60)
    hp, maxHP = self.getTargetHP ()
    assert (hp is not None) and (maxHP is not None)
    assert hp["armour"] < maxHP["armour"]
    self.assertEqual (hp["shield"], maxHP["shield"])

    self.mainLogger.info ("Killing character...")
    self.setTargetWP ({"x": 0, "y": 0})
    self.generate (30)
    hp, maxHP = self.getTargetHP ()
    assert (hp is None) and (maxHP is None)

    self.testReorg ()

  def testReorg (self):
    """
    Reorgs away most of the blocks (especially to resurrect the killed
    character).  Builds a small alternate chain.  Reorgs back to the
    original chain and verifies that the same state is returned.
    """

    self.mainLogger.info ("Testing a reorg...")
    originalState = self.getGameState ()

    self.rpc.xaya.invalidateblock (self.restoreBlock)

    self.setTargetWP ({"x": 0, "y": 0})
    self.generate (20)
    hp, maxHP = self.getTargetHP ()
    assert (hp is None) and (maxHP is None)

    self.rpc.xaya.reconsiderblock (self.restoreBlock)
    self.expectGameState (originalState)


if __name__ == "__main__":
  CombatDamageTest ().main ()
