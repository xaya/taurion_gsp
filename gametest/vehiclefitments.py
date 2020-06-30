#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020  Autonomous Worlds Ltd
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
Tests vehicle changes and fitments.
"""

from pxtest import PXTest


class VehicleFitmentsTest (PXTest):

  def run (self):
    self.collectPremine ()
    self.splitPremine ()

    self.mainLogger.info ("Setting up initial situation...")
    self.build ("ancient1", None, {"x": 0, "y": 0}, 0)
    building = 1001
    self.assertEqual (self.getBuildings ()[building].getType (), "ancient1")

    self.initAccount ("domob", "r")
    self.generate (1)
    self.dropIntoBuilding (building, "domob", {
      "chariot": 1,
      "free plating": 1,
      "lf bomb": 1,
    })
    self.createCharacters ("domob")
    self.generate (1)
    self.moveCharactersTo ({"domob": {"x": 30, "y": 0}})
    self.getCharacters ()["domob"].sendMove ({"eb": building})
    self.generate (1)

    self.mainLogger.info ("Changing vehicle...")
    self.getCharacters ()["domob"].sendMove ({"v": "chariot"})
    self.generate (1)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.data["vehicle"], "chariot")
    self.assertEqual (c.data["fitments"], [])
    self.assertEqual (c.data["combat"]["hp"], {
      "current": {"armour": 1000, "shield": 100},
      "max": {"armour": 1000, "shield": 100},
      "regeneration": 0.01,
    })
    self.assertEqual (len (c.data["combat"]["attacks"]), 2)

    self.mainLogger.info ("Adding fitments...")
    self.getCharacters ()["domob"].sendMove ({
      "fit": ["free plating", "lf bomb"],
    })
    self.generate (1)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.data["vehicle"], "chariot")
    self.assertEqual (c.data["fitments"], ["free plating", "lf bomb"])
    self.assertEqual (c.data["combat"]["hp"], {
      "current": {"armour": 1100, "shield": 100},
      "max": {"armour": 1100, "shield": 100},
      "regeneration": 0.01,
    })
    self.assertEqual (len (c.data["combat"]["attacks"]), 3)

    self.mainLogger.info ("Removing fitments...")
    self.getCharacters ()["domob"].sendMove ({"fit": []})
    self.generate (1)
    c = self.getCharacters ()["domob"]
    self.assertEqual (c.data["vehicle"], "chariot")
    self.assertEqual (c.data["fitments"], [])
    self.assertEqual (c.data["combat"]["hp"], {
      "current": {"armour": 1000, "shield": 100},
      "max": {"armour": 1000, "shield": 100},
      "regeneration": 0.01,
    })
    self.assertEqual (len (c.data["combat"]["attacks"]), 2)


if __name__ == "__main__":
  VehicleFitmentsTest ().main ()
