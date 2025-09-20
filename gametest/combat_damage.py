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
Tests dealing damage, regenerating the shield and killing characters.
"""

from pxtest import PXTest, offsetCoord


class CombatDamageTest (PXTest):

  def getTargetHP (self):
    """
    Returns the current and max HP struct for the given character name or None
    if the character does not exist.
    """

    chars = self.getCharacters ()

    if "target" not in chars:
      return None, None

    hp = chars["target"].data["combat"]["hp"]
    return hp["current"], hp["max"]

  def run (self):
    numAttackers = 5

    self.mainLogger.info ("Creating test characters...")
    self.initAccount ("target", "b")
    self.createCharacters ("target")
    self.initAccount ("attacker", "r")
    self.createCharacters ("attacker", 2)
    self.generate (1)
    self.changeCharacterVehicle ("attacker", "light attacker")

    # We use a known good position as offset for our test.
    self.offset = {"x": -1100, "y": 1042}
    self.inRange = offsetCoord ({"x": 0, "y": 0}, self.offset, False)
    outOfRange = offsetCoord ({"x": 0, "y": 20}, self.offset, False)
    self.moveCharactersTo ({
      "attacker": offsetCoord ({"x": 0, "y": 1}, self.offset, False),
      "attacker 2": offsetCoord ({"x": 1, "y": 0}, self.offset, False),
    })
    self.getTargetHP ()

    self.mainLogger.info ("Taking some damage...")
    self.moveCharactersTo ({"target": self.inRange})
    self.setCharactersHP ({"target": {"ma": 1000, "a": 1000, "s": 2}})
    self.generate (3)
    hp, maxHP = self.getTargetHP ()
    assert (hp is not None) and (maxHP is not None)
    assert hp["armour"] < maxHP["armour"]
    assert hp["shield"] < 2

    self.snapshot = self.env.snapshot ()

    self.mainLogger.info ("Regenerating shield...")
    self.moveCharactersTo ({"target": outOfRange})
    self.generate (60)
    hp, maxHP = self.getTargetHP ()
    assert (hp is not None) and (maxHP is not None)
    assert hp["armour"] < maxHP["armour"]
    self.assertEqual (hp["shield"], maxHP["shield"])

    self.mainLogger.info ("Killing character...")
    self.moveCharactersTo ({"target": self.inRange})
    self.setCharactersHP ({"target": {"a": 1, "s": 0}})
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

    self.snapshot.restore ()

    self.moveCharactersTo ({"target": self.inRange})
    self.setCharactersHP ({"target": {"a": 1, "s": 0}})
    self.generate (5)
    hp, maxHP = self.getTargetHP ()
    assert (hp is None) and (maxHP is None)


if __name__ == "__main__":
  CombatDamageTest ().main ()
