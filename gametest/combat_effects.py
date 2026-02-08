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
Tests combat with effects (e.g. retarder).
"""

from pxtest import PXTest, offsetCoord


class CombatEffectsTest (PXTest):

  def run (self):
    self.mainLogger.info ("Creating test characters...")
    self.initAccount ("attacker", "r")
    self.createCharacters ("attacker")
    self.initAccount ("target", "b")
    self.createCharacters ("target")
    self.initAccount ("friendly", "r")
    self.createCharacters ("friendly")
    self.generate (1)

    self.mainLogger.info ("Setting up basic situation...")
    self.changeCharacterVehicle ("attacker", "basetank", ["lf retarder"])
    self.changeCharacterVehicle ("target", "basetank", [])
    self.changeCharacterVehicle ("friendly", "basetank", [])
    self.moveCharactersTo ({
      "attacker": {"x": 0, "y": 1},
      "target": {"x": -50, "y": 0},
      "friendly": {"x": -48, "y": 0},
    })

    self.mainLogger.info ("Moving through retarding area...")
    c = self.getCharacters ()
    for key in ["target", "friendly"]:
      c[key].moveTowards ({"x": 50, "y": 0})
    self.generate (98)
    c = self.getCharacters ()
    self.assertEqual (c["friendly"].getPosition (), {"x": 50, "y": 0})
    self.assertEqual (c["target"].getPosition (), {"x": 46, "y": 0})


if __name__ == "__main__":
  CombatEffectsTest ().main ()
