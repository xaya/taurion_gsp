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
Tests friendly combat effects (ally shield projector).
"""

from pxtest import PXTest, offsetCoord


class CombatFriendlyTest (PXTest):

  def expectShield (self, charId, expected):
    """
    Expects that the shield HP (including the fractional part) of the
    given character match the expected value.
    """

    c = self.getCharacters ()[charId]
    actual = c.data["combat"]["hp"]["current"]["shield"]
    self.assertEqual (round (1000 * actual), round (1000 * expected))

  def run (self):
    self.mainLogger.info ("Creating test characters...")
    self.initAccount ("domob", "r")
    self.createCharacters ("domob", 2)
    self.generate (1)
    self.changeCharacterVehicle ("domob", "light attacker")
    self.changeCharacterVehicle ("domob 2", "light attacker",
                                 ["lf allyreplenish"])

    self.moveCharactersTo ({
      "domob": {"x": 0, "y": 2},
      "domob 2": {"x": 0, "y": 0},
    })
    self.setCharactersHP ({"domob": {"s": 0}})

    self.mainLogger.info ("Regenerating out of range...")
    self.generate (3)
    self.expectShield ("domob", 1.5)

    self.mainLogger.info ("Moving into range...")
    self.moveCharactersTo ({
      "domob": {"x": 0, "y": 1},
    })
    # Moving the character into range mines one block, which regenerates
    # normally and applies targeting.  From then on, the boosted rate
    # is applied.
    self.generate (3)
    self.expectShield ("domob", 2 + 3 * 0.5 * 1.15)

    self.mainLogger.info ("Moving out of range again...")
    self.moveCharactersTo ({
      "domob": {"x": 1, "y": 1},
    })
    # Moving out of range mines a block during whose regeneration phase
    # the effect is still in place (as the movement happens afterwards).
    # Then only normal regeneration is active.
    self.generate (5)
    self.expectShield ("domob", 2 + 4 * 0.5 * 1.15 + 5 * 0.5)


if __name__ == "__main__":
  CombatFriendlyTest ().main ()
