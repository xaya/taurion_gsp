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
Tests prospecting with detection of resources.  This only tests some basic
properties.  Detailed tests of the resource distribution logic are already
in the unit tests.
"""

from pxtest import PXTest

# Coordinate where there is no available resource.
NOTHING = {"x": -211, "y": -4064}

# Coordinates with just one type of resource.
ONLY_A = {"x": -3456, "y": -1215}
ONLY_F = {"x": -876, "y": -2015}

# Coordinate with "raw i" at max chance and "raw h" as well.
I_AND_H = {"x": -2428, "y": -3124}


class ProspectingResourcesTest (PXTest):

  def prospectAt (self, pos):
    """
    Prospects the region at the given coordinate and returns the found
    resource type and amount.
    """

    self.moveCharactersTo ({"domob": pos})
    self.getCharacters ()["domob"].sendMove ({"prospect": {}})
    self.generate (11)

    r = self.getRegionAt (pos)
    self.assertEqual (r.data["prospection"]["name"], "domob")
    return r.getResource ()

  def run (self):
    self.collectPremine ()

    self.initAccount ("domob", "r")
    c = self.createCharacters ("domob")
    self.generate (1)

    self.mainLogger.info ("Nothing to be found...")
    typ, amount = self.prospectAt (NOTHING)
    self.assertEqual ((typ, amount), ("raw a", 0))

    self.mainLogger.info ("Only type a to be found...")
    typ, amount = self.prospectAt (ONLY_A)
    self.assertEqual (typ, "raw a")
    assert amount > 0

    self.mainLogger.info ("Only type f to be found...")
    typ, amount = self.prospectAt (ONLY_F)
    self.assertEqual (typ, "raw f")
    assert amount > 0

    self.mainLogger.info ("Two types...")
    typ, amount = self.prospectAt (I_AND_H)
    found = {"raw h": False, "raw i": False}
    while True:
      assert amount > 0
      assert typ in found
      found[typ] = True

      if found["raw h"] and found["raw i"]:
        break

      blk = self.rpc.xaya.getbestblockhash ()
      self.rpc.xaya.invalidateblock (blk)
      self.generate (1)

      r = self.getRegionAt (I_AND_H)
      self.assertEqual (r.data["prospection"]["name"], "domob")
      typ, amount = r.getResource ()
      assert amount > 0


if __name__ == "__main__":
  ProspectingResourcesTest ().main ()
