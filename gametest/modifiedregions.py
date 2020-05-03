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
Tests the getregions RPC filtering for only recently modified regions.
"""

from pxtest import PXTest


class ModifiedRegionsTest (PXTest):

  def prospectAt (self, pos):
    """
    Prospects the region at the given position, and returns the ID of the
    region there as well as an "approximate" modified height for the region.
    """

    data = self.rpc.game.getregionat (coord=pos)

    self.moveCharactersTo ({"prospector": pos})
    h = self.rpc.xaya.getblockcount ()

    self.getCharacters ()["prospector"].sendMove ({"prospect": {}})
    self.generate (15)

    return data["id"], h

  def queryRegions (self, h):
    """
    Queries the regions since the given height, and returns the set of
    region IDs in the result set.
    """

    data = self.getRpc ("getregions", fromheight=h)
    return set ({r["id"] for r in data})

  def run (self):
    self.collectPremine ()

    self.initAccount ("prospector", "r")
    self.createCharacters ("prospector")
    self.generate (1)

    r1, h1 = self.prospectAt ({"x": 10, "y": 10})
    r2, h2 = self.prospectAt ({"x": 100, "y": -42})
    r3, h3 = self.prospectAt ({"x": -10, "y": 50})

    assert r1 != r2
    assert r1 != r3

    self.assertEqual (self.queryRegions (0), set ([r1, r2, r3]))
    self.assertEqual (self.queryRegions (h1), set ([r1, r2, r3]))
    self.assertEqual (self.queryRegions (h2), set ([r2, r3]))
    self.assertEqual (self.queryRegions (h3), set ([r3]))
    self.assertEqual (self.queryRegions (1000), set ([]))

    self.expectError (3, ".*too low for current block height.*",
                      self.queryRegions, -10000)


if __name__ == "__main__":
  ModifiedRegionsTest ().main ()
