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

  def run (self):
    self.generate (110);

    numAttackers = 5

    self.mainLogger.info ("Creating test characters...")
    self.createCharacter (TARGET, "b")
    self.createCharacter ("attacker 1", "r")
    self.createCharacter ("attacker 2", "r")
    self.generate (1)

    # We use a known good position as offset for our test.
    self.offset = {"x": -1100, "y": 1042}
    self.inRange = offsetCoord ({"x": 0, "y": 0}, self.offset, False)
    outOfRange = offsetCoord ({"x": 0, "y": 20}, self.offset, False)
    self.moveCharactersTo ({
      "attacker 1": offsetCoord ({"x": 0, "y": 1}, self.offset, False),
      "attacker 2": offsetCoord ({"x": 1, "y": 0}, self.offset, False),
    })
    self.getTargetHP ()

    self.mainLogger.info ("Taking some damage...")
    self.moveCharactersTo ({TARGET: self.inRange})
    self.setCharactersHP ({TARGET: {"ma": 1000, "a": 1000, "s": 2}})
    self.generate (3)
    hp, maxHP = self.getTargetHP ()
    assert (hp is not None) and (maxHP is not None)
    assert hp["armour"] < maxHP["armour"]
    assert hp["shield"] < 2

    self.restoreBlock = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Regenerating shield...")
    self.moveCharactersTo ({TARGET: outOfRange})
    self.generate (60)
    hp, maxHP = self.getTargetHP ()
    assert (hp is not None) and (maxHP is not None)
    assert hp["armour"] < maxHP["armour"]
    self.assertEqual (hp["shield"], maxHP["shield"])

    self.mainLogger.info ("Killing character...")
    self.moveCharactersTo ({TARGET: self.inRange})
    self.setCharactersHP ({TARGET: {"a": 1, "s": 0}})
    self.generate (5)
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

    self.moveCharactersTo ({TARGET: self.inRange})
    self.setCharactersHP ({TARGET: {"a": 1, "s": 0}})
    self.generate (5)
    hp, maxHP = self.getTargetHP ()
    assert (hp is None) and (maxHP is None)

    self.rpc.xaya.reconsiderblock (self.restoreBlock)
    self.expectGameState (originalState)


if __name__ == "__main__":
  CombatDamageTest ().main ()
