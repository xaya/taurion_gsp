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
    # Red approaches along the x axis, where it is at the same distance from
    # both greens, so that it picks one of them at random once it gets in
    # range (and keeps that target afterwards).  Each roll thus starts from
    # the block before.
    self.moveCharactersTo ({
      "red": offsetCoord ({"x": 25, "y": 0}, self.offset, False),
    })
    self.getCharacters ()["red"].moveTowards (
        offsetCoord ({"x": 1, "y": 0}, self.offset, False))
    while self.getCharacters ()["red"].getPosition ()["x"] > 13:
      self.generate (1)
    assert self.getTargetCharacter ("red") is None
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

    self.mainLogger.info ("Testing a target that founds a building...")
    # Founding puts red into its new building during move processing,
    # before green deals damage to its old target (in the same superblock,
    # or in the next one if red founds between superblocks).  That used to
    # halt the GSP.
    site = offsetCoord ({"x": 5, "y": 0}, self.offset, False)
    self.moveCharactersTo ({"red": site})
    self.setCharactersHP ({
      "red": {"ma": 1000, "a": 1000, "ms": 1000, "s": 1000},
    })
    self.dropLoot (site, {"foo": 2})
    self.getCharacters ()["red"].sendMove ({"pu": {"f": {"foo": 2}}})
    self.generate (1)
    self.assertEqual (self.getTargetCharacter ("green"), "red")
    beforeFounding = self.env.snapshot ()
    # Between superblocks first:  restoring does not reset the mock time.
    for superblock in [False, True]:
      beforeFounding.restore ()
      height = self.getSuperblockHeight ()
      self.getCharacters ()["red"].sendMove ({"fb": {"t": "huesli", "rot": 0}})
      self.generate (1, superblocks=superblock)
      assert self.getCharacters ()["red"].isInBuilding ()
      if not superblock:
        self.assertEqual (self.getSuperblockHeight (), height)
        self.assertEqual (self.getTargetCharacter ("green"), "red")
        self.generate (1)
      target = self.getCharacters ()["green"].data["combat"]["target"]
      self.assertEqual (target, {"type": "building",
                                 "id": max (self.getBuildings ())})

    self.mainLogger.info ("Testing a target in a no-combat zone...")
    # After founding between superblocks, red could also leave its building
    # again into a no-combat zone before green deals damage to it.  That used
    # to halt the GSP.  Here green and red are teleported next to and into one
    # instead, at the start of the superblock that deals the damage.
    beforeFounding.restore ()
    self.env.setMockTime (self.w3.eth.get_block ("latest")["timestamp"])
    height = self.getSuperblockHeight ()
    self.getCharacters ()["red"].sendMove ({"fb": {"t": "huesli", "rot": 0}})
    self.generate (1, superblocks=False)
    assert self.getCharacters ()["red"].isInBuilding ()
    self.assertEqual (self.getSuperblockHeight (), height)
    self.assertEqual (self.getTargetCharacter ("green"), "red")
    self.moveCharactersTo ({
      "green": {"x": 2042, "y": 13},
      "red": {"x": 2042, "y": 10},
    })
    assert self.getTargetCharacter ("green") is None


if __name__ == "__main__":
  CombatTargetTest ().main ()
