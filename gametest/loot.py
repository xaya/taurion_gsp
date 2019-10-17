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
Tests handling of ground loot:  Placing it in god mode, picking it up by
characters, dropping stuff by characters.
"""

from pxtest import PXTest


class LootTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.mainLogger.info ("Dropping loot in god mode...")
    self.dropLoot ({"x": 1, "y": 2}, {"zerospace": 5, "bar": 10})
    self.dropLoot ({"x": 1, "y": 2}, {"zerospace": 5})
    self.dropLoot ({"x": -1, "y": 20}, {"zerospace": 5})
    self.assertEqual (self.getRpc ("getgroundloot"), [
      {
        "position": {"x": -1, "y": 20},
        "inventory":
          {
            "fungible": {"zerospace": 5},
          },
      },
      {
        "position": {"x": 1, "y": 2},
        "inventory":
          {
            "fungible": {"bar": 10, "zerospace": 10},
          },
      },
    ])

    self.mainLogger.info ("Picking up loot with a character...")
    self.initAccount ("red", "r")
    self.createCharacters ("red")
    self.generate (1)
    self.moveCharactersTo ({"red": {"x": 1, "y": 2}})
    self.generate (1)
    self.getCharacters ()["red"].sendMove ({"pu": {"f": {"zerospace": 1000}}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["red"].getFungibleInventory (), {
      "zerospace": 10,
    })
    self.assertEqual (self.getRpc ("getgroundloot"), [
      {
        "position": {"x": -1, "y": 20},
        "inventory":
          {
            "fungible": {"zerospace": 5},
          },
      },
      {
        "position": {"x": 1, "y": 2},
        "inventory":
          {
            "fungible": {"bar": 10},
          },
      },
    ])

    self.mainLogger.info ("Dropping loot with a character...")
    self.moveCharactersTo ({"red": {"x": -1, "y": 20}})
    self.getCharacters ()["red"].sendMove ({"drop": {"f": {"zerospace": 1}}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["red"].getFungibleInventory (), {
      "zerospace": 9,
    })
    self.assertEqual (self.getRpc ("getgroundloot"), [
      {
        "position": {"x": -1, "y": 20},
        "inventory":
          {
            "fungible": {"zerospace": 6},
          },
      },
      {
        "position": {"x": 1, "y": 2},
        "inventory":
          {
            "fungible": {"bar": 10},
          },
      },
    ])

    self.mainLogger.info ("Cargo limit for picking loot up...")
    self.initAccount ("cargo", "r")
    self.dropLoot ({"x": 0, "y": 0}, {"foo": 10})
    self.createCharacters ("cargo", 1)
    self.generate (1)
    self.moveCharactersTo ({"cargo": {"x": 0, "y": 0}})
    self.getCharacters ()["cargo"].sendMove ({"pu": {"f": {"foo": 100}}})
    self.generate (1)
    c = self.getCharacters ()["cargo"]
    self.assertEqual (c.getFungibleInventory (), {
      "foo": 2,
    })
    c.expectPartial ({
      "cargospace":
        {
          "total": 20,
          "used": 20,
          "free": 0,
        },
    })
    self.assertEqual (self.getRpc ("getgroundloot"), [
      {
        "position": {"x": -1, "y": 20},
        "inventory":
          {
            "fungible": {"zerospace": 6},
          },
      },
      {
        "position": {"x": 0, "y": 0},
        "inventory":
          {
            "fungible": {"foo": 8},
          },
      },
      {
        "position": {"x": 1, "y": 2},
        "inventory":
          {
            "fungible": {"bar": 10},
          },
      },
    ])

    self.mainLogger.info ("Death drops of character inventories...")
    self.initAccount ("green", "g")
    self.createCharacters ("green")
    self.generate (1)
    self.moveCharactersTo ({
      "red": {"x": 100, "y": 100},
      "green": {"x": 100, "y": 100},
    })
    self.setCharactersHP ({
      "red": {"a": 1, "s": 0},
    })
    self.assertEqual (self.getCharacters ()["red"].getFungibleInventory (), {
      "zerospace": 9,
    })
    self.getCharacters ()["green"].sendMove ({"pu": {"f": {"zerospace": 5}}})
    self.generate (1)
    chars = self.getCharacters ()
    assert "red" not in chars
    self.assertEqual (chars["green"].getFungibleInventory (), {
      "zerospace": 5,
    })
    self.assertEqual (self.getRpc ("getgroundloot"), [
      {
        "position": {"x": -1, "y": 20},
        "inventory":
          {
            "fungible": {"zerospace": 6},
          },
      },
      {
        "position": {"x": 0, "y": 0},
        "inventory":
          {
            "fungible": {"foo": 8},
          },
      },
      {
        "position": {"x": 1, "y": 2},
        "inventory":
          {
            "fungible": {"bar": 10},
          },
      },
      {
        "position": {"x": 100, "y": 100},
        "inventory":
          {
            "fungible": {"zerospace": 4},
          },
      },
    ])


if __name__ == "__main__":
  LootTest ().main ()
