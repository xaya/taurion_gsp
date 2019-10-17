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
Tests prospecting with detection of resources.
"""

from pxtest import PXTest


class ProspectingResourcesTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.initAccount ("domob", "r")
    c = self.createCharacters ("domob")
    self.generate (1)
    pos = {"x": -1000, "y": 1000}
    self.moveCharactersTo ({"domob": pos})
    self.getCharacters ()["domob"].sendMove ({"prospect": {}})
    self.generate (11)

    r = self.getRegionAt (pos)
    self.assertEqual (r.data["prospection"]["name"], "domob")
    typ, amount = r.getResource ()
    assert typ in ["raw a", "raw b"]
    assert amount > 0

    # FIXME: For now, this test is super limited.  We should extend it
    # and maybe test that different places on the map give different resources
    # (at least in a simple form).


if __name__ == "__main__":
  ProspectingResourcesTest ().main ()
