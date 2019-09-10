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
Tests the god-mode commands.
"""

from pxtest import PXTest


class GodModeTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.initAccount ("domob", "r")
    self.createCharacter ("domob", "r")
    self.generate (1)
    c = self.getCharacters ()["domob"]
    pos = c.getPosition ()
    idStr = c.getIdStr ()

    self.mainLogger.info ("Testing teleport...")
    target = {"x": 28, "y": 9}
    assert pos != target
    self.adminCommand ({"god": {"teleport": {idStr: target}}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["domob"].getPosition (), target)

    self.mainLogger.info ("Testing sethp...")
    self.adminCommand ({
      "god":
        {
          "sethp":
            {
              idStr: {"a": 32, "s": 15, "ma": 100, "ms": 90},
            },
        },
    })
    self.generate (1)
    hp = self.getCharacters ()["domob"].data["combat"]["hp"]
    self.assertEqual (hp["current"], {"armour": 32, "shield": 15})
    self.assertEqual (hp["max"], {"armour": 100, "shield": 90})
    

if __name__ == "__main__":
  GodModeTest ().main ()
