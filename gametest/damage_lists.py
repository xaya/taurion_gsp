#!/usr/bin/env python

from pxtest import PXTest, offsetCoord

"""
Tests tracking of damage lists.
"""


class DamageListsTest (PXTest):

  def getAttackers (self, name):
    """
    Returns the list of attackers of the character with the given owner
    name as a Python "set".
    """

    combat = self.getCharacters ()[name].data["combat"]
    if "attackers" not in combat:
      return set ()

    res = set (combat["attackers"])
    assert len (res) > 0
    return res

  def expectAttackers (self, name, expected):
    """
    Expect that the attackers for the given character are the characters
    with the given names.
    """

    chars = self.getCharacters ()
    expectedIds = [chars[e].getId () for e in expected]

    self.assertEqual (self.getAttackers (name), set (expectedIds))

  def run (self):
    self.generate (110);

    self.mainLogger.info ("Creating test characters...")
    self.createCharacter ("target", "b")
    self.createCharacter ("attacker 1", "r")
    self.createCharacter ("attacker 2", "r")
    self.generate (1)

    # We use a known good position as offset and move the characters
    # nearby so they are attacking each other.
    self.offset = {"x": -1100, "y": 1042}
    self.moveCharactersTo ({
      "target": self.offset,
      "attacker 1": offsetCoord ({"x": -5, "y": 0}, self.offset, False),
      "attacker 2": offsetCoord ({"x": 5, "y": 0}, self.offset, False),
    })

    # Check state after exactly one round of attacks.
    self.mainLogger.info ("Attacking and testing damage lists...")
    self.generate (1)
    self.expectAttackers ("target", ["attacker 1", "attacker 2"])
    if len (self.getAttackers ("attacker 1")) > 0:
      self.expectAttackers ("attacker 1", ["target"])
      self.expectAttackers ("attacker 2", [])
    else:
      self.expectAttackers ("attacker 1", [])
      self.expectAttackers ("attacker 2", ["target"])

    # Move the target out of range and verify timeout of the damage list.
    # Note that the block generated during moveCharactersTo still performs
    # an attack before processing the move, so that the damage list entries
    # are refreshed.
    self.mainLogger.info ("Letting damage list time out...")
    self.moveCharactersTo ({
      "target": offsetCoord ({"x": -100, "y": 0}, self.offset, False),
    })
    self.generate (99)
    self.expectAttackers ("target", ["attacker 1", "attacker 2"])
    self.generate (1)
    self.expectAttackers ("target", [])


if __name__ == "__main__":
  DamageListsTest ().main ()
