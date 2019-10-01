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
    self.dropLoot ({"x": 1, "y": 2}, {"foo": 5, "bar": 10})
    self.dropLoot ({"x": 1, "y": 2}, {"foo": 5})
    self.dropLoot ({"x": -1, "y": 20}, {"foo": 5})
    self.assertEqual (self.getRpc ("getgroundloot"), [
      {
        "position": {"x": -1, "y": 20},
        "inventory":
          {
            "fungible": {"foo": 5},
          },
      },
      {
        "position": {"x": 1, "y": 2},
        "inventory":
          {
            "fungible": {"bar": 10, "foo": 10},
          },
      },
    ])


if __name__ == "__main__":
  LootTest ().main ()
