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
Tests spawning of characters.
"""

from pxtest import PXTest, offsetCoord


class SpawnTest (PXTest):

  def run (self):
    self.collectPremine ()

    # Verify that characters of each faction can be spawned, and will end up
    # in their respective starting city building.
    for f in ["r", "g", "b"]:
      self.initAccount (f, f)
      self.createCharacters (f)
    self.generate (1)
    buildings = self.getBuildings ()
    chars = self.getCharacters ()
    for f in ["r", "g", "b"]:
      c = chars[f]
      self.assertEqual (c.isInBuilding (), True)
      b = buildings[c.getBuildingId ()]
      self.assertEqual (b.getType (), "%s ss" % f)


if __name__ == "__main__":
  SpawnTest ().main ()
