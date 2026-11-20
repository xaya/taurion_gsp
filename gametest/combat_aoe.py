#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020-2025  Autonomous Worlds Ltd
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
Tests combat with AoE attacks.
"""

from pxtest import PXTest, offsetCoord


class CombatAoETest (PXTest):

  def resetHP (self):
    """
    Resets the HP of all target characters to the original value,
    so we can start a fresh test.
    """

    self.setCharactersHP ({
      nm: {"ma": 100, "a": 100, "ms": 0, "s": 0}
      for nm in self.targetNames
    })

  def expectDamaged (self, lst):
    """
    Expects that the given characters out of all targets have been damaged,
    and that at the same HP for all of them.
    """

    c = self.getCharacters ()
    hp = c[lst[0]].data["combat"]["hp"]["current"]["armour"]
    assert hp < 100

    lst = set (lst)
    for nm in self.targetNames:
      if nm in lst:
        self.assertEqual (c[nm].data["combat"]["hp"]["current"]["armour"], hp)
      else:
        self.assertEqual (c[nm].data["combat"]["hp"]["current"]["armour"], 100)

  def run (self):
    self.mainLogger.info ("Creating test characters...")
    self.initAccount ("attacker", "r")
    self.createCharacters ("attacker")
    self.generate (1)
    self.changeCharacterVehicle ("attacker", "basetank")

    # We set up four potential targets:  Two within the "lf bomb" range
    # of 3 (a closer and b further).  Then one within the "missile" area
    # around each of those (c for a and d for b).
    self.targetNames = ["a", "b", "c", "d"]
    for nm in self.targetNames:
      self.initAccount (nm, "g")
      self.createCharacters (nm)
    # Extra targets for testing the limit of area targets, one less than it.
    # They stay out of the way (at their spawn) until then.
    limit = self.roConfig ().params.max_area_targets
    self.initAccount ("e", "g")
    self.createCharacters ("e", limit - 1)
    self.generate (1)
    extraNames = ["e"] + ["e %d" % i for i in range (2, limit)]
    for nm in self.targetNames + extraNames:
      self.changeCharacterVehicle (nm, "basetank")
    self.moveCharactersTo ({
      "a": {"x": 2, "y": 0},
      "b": {"x": -3, "y": 0},
      "c": {"x": 2, "y": 2},
      "d": {"x": -3, "y": -2},
    })

    self.mainLogger.info ("Testing AoE centred on attacker...")
    self.changeCharacterVehicle ("attacker", "basetank", ["lf bomb"])
    self.resetHP ()
    self.moveCharactersTo ({"attacker": {"x": 0, "y": 0}})
    self.generate (2)
    self.expectDamaged (["a", "b"])

    self.mainLogger.info ("Testing AoE centred on target...")
    self.changeCharacterVehicle ("attacker", "basetank", ["lf missile"])
    self.resetHP ()
    self.moveCharactersTo ({"attacker": {"x": 0, "y": 0}})
    self.generate (2)
    self.expectDamaged (["a", "c"])

    self.mainLogger.info ("Testing the limit of targets...")
    # An area attack hits only the closest targets up to max_area_targets.
    # With the extra targets next to the attacker, those are them and a.
    self.changeCharacterVehicle ("attacker", "basetank", ["lf bomb"])
    neighbours = [
      {"x": 1, "y": 0},
      {"x": 0, "y": 1},
      {"x": -1, "y": 1},
      {"x": -1, "y": 0},
      {"x": 0, "y": -1},
      {"x": 1, "y": -1},
    ]
    assert len (extraNames) <= len (neighbours)
    self.moveCharactersTo (dict (zip (extraNames, neighbours)))
    self.targetNames.extend (extraNames)
    self.resetHP ()
    self.moveCharactersTo ({"attacker": {"x": 0, "y": 0}})
    self.generate (2)
    self.expectDamaged (["a"] + extraNames)


if __name__ == "__main__":
  CombatAoETest ().main ()
