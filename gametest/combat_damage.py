#!/usr/bin/env python

from pxtest import PXTest

"""
Tests dealing damage, regenerating the shield and killing characters.
"""


class CombatDamageTest (PXTest):

  def getHP (self, character):
    """
    Returns the current and max HP struct for the given character name or None
    if the character does not exist.
    """

    chars = self.getCharacters ()

    if character not in chars:
      return None, None

    hp = chars[character].data["combat"]["hp"]
    return hp["current"], hp["max"]

  def run (self):
    self.generate (101);

    self.mainLogger.info ("Creating test characters...")
    self.createCharacter ("domob", "target", "b")
    self.generate (1)
    c = self.getCharacters ()["target"]
    c.sendMove ({"wp": [{"x": 5, "y": 0}]})
    self.generate (10)
    for i in range (10):
      self.createCharacter ("name %d" % i, "char %d" % i, "r")
    self.generate (1)

    self.mainLogger.info ("Taking some damage...")
    c = self.getCharacters ()["target"]
    c.sendMove ({"wp": [{"x": 11, "y": 0}]})
    self.generate (10)
    hp, maxHP = self.getHP ("target")
    assert (hp is not None) and (maxHP is not None)
    assert hp["armour"] < maxHP["armour"]
    assert hp["shield"] < maxHP["shield"]

    self.mainLogger.info ("Regenerating shield...")
    self.generate (60)
    hp, maxHP = self.getHP ("target")
    assert (hp is not None) and (maxHP is not None)
    assert hp["armour"] < maxHP["armour"]
    self.assertEqual (hp["shield"], maxHP["shield"])

    self.mainLogger.info ("Killing character...")
    c = self.getCharacters ()["target"]
    c.sendMove ({"wp": [{"x": 0, "y": 0}]})
    self.generate (15)
    hp, maxHP = self.getHP ("target")
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

    blk = self.rpc.xaya.getblockhash (125)
    self.rpc.xaya.invalidateblock (blk)

    c = self.getCharacters ()["target"]
    c.sendMove ({"wp": [{"x": 0, "y": 0}]})
    self.generate (20)
    hp, maxHP = self.getHP ("target")
    assert (hp is None) and (maxHP is None)

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)


if __name__ == "__main__":
  CombatDamageTest ().main ()
