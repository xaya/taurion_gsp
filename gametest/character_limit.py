#!/usr/bin/env python

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019  Autonomous Worlds Ltd
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
Tests the behaviour of the per-account character limit.
"""

from pxtest import PXTest

# Maximum number of characters per account.
LIMIT = 20


class CharacterLimitTest (PXTest):

  def countForOwner (self, owner):
    """
    Returns the number of characters of the given owner account.
    """

    chars = self.getCharacters ()

    res = 0
    for c in chars.values ():
      if c.getOwner () == owner:
        res += 1

    return res

  def run (self):
    self.collectPremine ()

    self.initAccount ("domob", "r")
    self.initAccount ("other", "r")
    self.generate (1)

    self.mainLogger.info ("Creating characters to the limit...")
    self.createCharacters ("domob", LIMIT)
    self.generate (1)
    self.assertEqual (self.countForOwner ("domob"), LIMIT)

    self.mainLogger.info ("Extra characters are not possible...")
    self.createCharacters ("domob", LIMIT)
    self.createCharacters ("other")
    self.generate (1)
    self.getCharacters ()["other"].sendMove ({"send": "domob"})
    self.generate (1)
    self.assertEqual (self.countForOwner ("domob"), LIMIT)
    self.assertEqual (self.countForOwner ("other"), 1)

    self.mainLogger.info ("Killing some characters...")
    self.initAccount ("attacker", "g")
    self.createCharacters ("attacker")
    self.setCharactersHP ({
      "domob 2": {"a": 1, "s": 0},
      "domob 5": {"a": 1, "s": 0},
    })
    self.moveCharactersTo ({
      "domob 2": {"x": 1, "y": -1},
      "domob 5": {"x": -1, "y": 1},
      "attacker": {"x": 0, "y": 0},
    })
    self.generate (10)
    self.assertEqual (self.countForOwner ("domob"), LIMIT - 2)

    self.mainLogger.info ("Limit should be relaxed now...")
    self.createCharacters ("domob")
    self.getCharacters ()["other"].sendMove ({"send": "domob"})
    self.generate (1)
    self.assertEqual (self.countForOwner ("domob"), LIMIT)
    self.assertEqual (self.countForOwner ("other"), 0)

if __name__ == "__main__":
  CharacterLimitTest ().main ()
