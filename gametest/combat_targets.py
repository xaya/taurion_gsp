#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2025  Autonomous Worlds Ltd
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
Tests the target selection aspect of combat.
"""

from pxtest import PXTest, offsetCoord


class CombatTargetTest (PXTest):

  def getTargetCharacter (self, owner):
    """
    Looks up the target of the given character (by owner).  Returns None
    if there is none.  If there is one, verify that it refers to a character
    and return that character's name (rather than ID).
    """

    chars = self.getCharacters ()
    assert owner in chars
    c = chars[owner]

    if "combat" not in c.data:
      return None
    if "target" not in c.data["combat"]:
      return None

    t = c.data["combat"]["target"]
    self.assertEqual (t["type"], "character")

    charId = t["id"]
    for nm, val in chars.items ():
      if val.getId () == charId:
        return nm

    raise AssertionError ("Character with id %d not found" % charId)

  def run (self):
    self.mainLogger.info ("Creating test characters...")
    self.initAccount ("red", "r")
    self.createCharacters ("red")
    self.initAccount ("green", "g")
    self.createCharacters ("green", 2)
    self.generate (1)
    self.changeCharacterVehicle ("red", "light attacker")
    self.changeCharacterVehicle ("green", "light attacker")
    self.changeCharacterVehicle ("green 2", "light attacker")

    # We use a well-defined position as coordinate offset,
    # so that the test is independent of the actual starting positions
    # of the characters.
    self.offset = {"x": 0, "y": 0}

    # Move all other characters in range before continuing with
    # the rest of the test.
    self.moveCharactersTo ({
      "red": offsetCoord ({"x": 1, "y": 0}, self.offset, False),
      "green": self.offset,
      "green 2": offsetCoord ({"x": 0, "y": 1}, self.offset, False),
    })

    self.mainLogger.info ("Testing randomised target selection...")
    cnts = {"green": 0, "green 2": 0}
    rolls = 10
    snapshot = self.env.snapshot ()
    for _ in range (rolls):
      self.generate (1)
      self.assertEqual (self.getTargetCharacter ("green"), "red")
      self.assertEqual (self.getTargetCharacter ("green 2"), "red")
      fooTarget = self.getTargetCharacter ("red")
      assert fooTarget is not None
      assert fooTarget in cnts
      cnts[fooTarget] += 1
      # Invalidate the last block so that we reroll the randomisation
      # with the next generated block.
      snapshot.restore ()
    for key, cnt in cnts.items ():
      self.log.info ("Target %s selected %d / %d times" % (key, cnt, rolls))
      assert cnt > 0

    self.mainLogger.info ("Testing target selection when moving into range...")
    c = self.getCharacters ()["red"]
    self.moveCharactersTo ({
      "red": offsetCoord ({"x": 13, "y": 0}, self.offset, False),
    })
    assert self.getTargetCharacter ("green") is None
    # Note that we can move a directly to the offset coordinate (where also
    # b is), since a and b are of different factions.
    c.moveTowards (self.offset)
    self.generate (1)
    self.assertEqual (self.getCharacters ()["red"].getPosition (),
                      offsetCoord ({"x": 10, "y": 0}, self.offset, False))
    self.assertEqual (self.getTargetCharacter ("green"), "red")


if __name__ == "__main__":
  CombatTargetTest ().main ()
