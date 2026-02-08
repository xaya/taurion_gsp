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
Tests the fame and kills update of accounts.
"""

from pxtest import PXTest


class FameTest (PXTest):

  def run (self):
    self.mainLogger.info ("Characters killing each other at the same time...")
    self.initAccount ("foo", "r")
    self.createCharacters ("foo")
    self.initAccount ("bar", "g")
    self.createCharacters ("bar")
    self.generate (1)
    self.changeCharacterVehicle ("foo", "light attacker")
    self.changeCharacterVehicle ("bar", "light attacker")
    self.moveCharactersTo ({
      "foo": {"x": 0, "y": 0},
      "bar": {"x": 0, "y": 0},
    })
    self.setCharactersHP ({
      "foo": {"a": 1, "s": 0},
      "bar": {"a": 1, "s": 0},
    })
    self.generate (1)
    chars = self.getCharacters ()
    assert "foo" not in chars
    assert "bar" not in chars
    accounts = self.getAccounts ()
    self.assertEqual (accounts["foo"].data["kills"], 1)
    self.assertEqual (accounts["foo"].data["fame"], 100)
    self.assertEqual (accounts["bar"].data["kills"], 1)
    self.assertEqual (accounts["bar"].data["fame"], 100)

    self.mainLogger.info ("Multiple killers...")
    self.initAccount ("red", "r")
    self.createCharacters ("red", 2)
    self.initAccount ("green", "g")
    self.createCharacters ("green")
    self.initAccount ("blue", "b")
    self.createCharacters ("blue")
    self.generate (1)
    self.changeCharacterVehicle ("red", "light attacker")
    self.changeCharacterVehicle ("red 2", "light attacker")
    self.changeCharacterVehicle ("green", "light attacker")
    self.changeCharacterVehicle ("blue", "light attacker")
    self.moveCharactersTo ({
      "blue": {"x": 0, "y": 0},
      "red": {"x": 5, "y": 0},
      "red 2": {"x": 0, "y": 5},
      "green": {"x": -5, "y": 0},
    })
    self.setCharactersHP ({
      "blue": {"a": 1, "s": 0},
    })
    self.generate (1)
    chars = self.getCharacters ()
    assert "blue" not in chars
    assert "red" in chars
    assert "red 2" in chars
    assert "green" in chars
    accounts = self.getAccounts ()
    self.assertEqual (accounts["red"].data["kills"], 1)
    self.assertEqual (accounts["red"].data["fame"], 150)
    self.assertEqual (accounts["green"].data["kills"], 1)
    self.assertEqual (accounts["green"].data["fame"], 150)
    self.assertEqual (accounts["blue"].data["kills"], 0)
    self.assertEqual (accounts["blue"].data["fame"], 0)

    self.mainLogger.info ("Many characters for a name...")
    armySize = 10
    self.initAccount ("army", "r")
    self.createCharacters ("army", armySize)
    self.initAccount ("other army", "r")
    self.createCharacters ("other army", armySize)
    self.initAccount ("target", "b")
    self.createCharacters ("target", 2)
    self.generate (1)
    mv = {
      "target": {"x": 100, "y": 0},
      "target 2": {"x": -100, "y": 0},
    }
    for i in range (0, armySize):
      suff = ""
      if i > 0:
        suff = " %d" % (i + 1)
      mv["army" + suff] = {"x": 101, "y": i - armySize // 2}
      mv["other army" + suff] = {"x": -101, "y": i - armySize // 2}
      self.changeCharacterVehicle ("army" + suff, "light attacker")
      self.changeCharacterVehicle ("other army" + suff, "light attacker")
    self.moveCharactersTo (mv)
    self.setCharactersHP ({
      "target": {"a": 1, "s": 0},
      "target 2": {"a": 1, "s": 0},
    })
    self.generate (1)
    accounts = self.getAccounts ()
    chars = self.getCharacters ()
    assert "target" not in chars
    assert "target 2" not in chars
    self.assertEqual (accounts["army"].data["kills"], 1)
    self.assertEqual (accounts["army"].data["fame"], 200)
    self.assertEqual (accounts["other army"].data["kills"], 1)
    self.assertEqual (accounts["other army"].data["fame"], 200)
    self.assertEqual (accounts["target"].data["kills"], 0)
    self.assertEqual (accounts["target"].data["fame"], 0)


if __name__ == "__main__":
  FameTest ().main ()
