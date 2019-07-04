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

from pxtest import PXTest

"""
Tests the getregionat RPC command.
"""


class GetRegionAtTest (PXTest):

  def run (self):
    getregionat = self.rpc.game.getregionat

    # Verify exceptions for invalid arguments.
    outOfMap = {"x": -10000, "y": 0}
    self.expectError (-1, "coord is not a valid coordinate",
                      getregionat, coord={})
    self.expectError (2, "coord is outside the game map",
                      getregionat, coord=outOfMap)

    # Call the method successfully and verify that we get "reasonable" results.
    testCoord = {"x": 42, "y": 100}
    res = getregionat (coord=testCoord)
    assert res["id"] >= 0 and res["id"] < 700000
    assert len (res["tiles"]) > 10
    found = False
    for t in res["tiles"]:
      if t == testCoord:
        assert not found
        found = True
      res2 = getregionat (coord=t)
      self.assertEqual (res, res2)
    assert found


if __name__ == "__main__":
  GetRegionAtTest ().main ()
