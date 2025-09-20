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
Tests tracking of damage lists.
"""

from pxtest import PXTest, offsetCoord


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
    self.mainLogger.info ("Creating test characters...")
    self.initAccount ("target", "b")
    self.createCharacters ("target")
    self.initAccount ("attacker", "r")
    self.createCharacters ("attacker", 2)
    self.generate (1)
    self.changeCharacterVehicle ("target", "light attacker")
    self.changeCharacterVehicle ("attacker", "light attacker")
    self.changeCharacterVehicle ("attacker 2", "light attacker")

    # We use a known good position as offset and move the characters
    # nearby so they are attacking each other.
    self.offset = {"x": -1100, "y": 1042}
    self.moveCharactersTo ({
      "target": self.offset,
      "attacker": offsetCoord ({"x": -5, "y": 0}, self.offset, False),
      "attacker 2": offsetCoord ({"x": 5, "y": 0}, self.offset, False),
    })

    # Check state after exactly one round of attacks.
    self.mainLogger.info ("Attacking and testing damage lists...")
    self.generate (1)
    self.expectAttackers ("target", ["attacker", "attacker 2"])
    if len (self.getAttackers ("attacker")) > 0:
      self.expectAttackers ("attacker", ["target"])
      self.expectAttackers ("attacker 2", [])
    else:
      self.expectAttackers ("attacker", [])
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
    self.expectAttackers ("target", ["attacker", "attacker 2"])
    self.generate (1)
    self.expectAttackers ("target", [])


if __name__ == "__main__":
  DamageListsTest ().main ()
