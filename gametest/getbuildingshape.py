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
Tests the getbuildingshape RPC command.
"""

from pxtest import PXTest


class GetBuildingShapeTest (PXTest):

  def run (self):
    getbuildingshape = self.rpc.game.getbuildingshape

    # Verify exceptions for invalid arguments.
    self.expectError (-1, "centre is not a valid coordinate",
                      getbuildingshape, type="huesli", centre={}, rot=3)
    self.expectError (-1, "rot is outside the valid range",
                      getbuildingshape, type="huesli",
                      centre={"x": 1, "y": 2}, rot=6)
    self.expectError (-1, "unknown building type",
                      getbuildingshape, type="invalid",
                      centre={"x": 1, "y": 2}, rot=0)

    # Valid result.
    self.assertEqual (getbuildingshape (type="checkmark",
                                        centre={"x": -1, "y": 5}, rot=2),
                      [{"x": -1, "y": 5},
                       {"x": -1, "y": 4},
                       {"x": 0, "y": 4},
                       {"x": 1, "y": 3}])


if __name__ == "__main__":
  GetBuildingShapeTest ().main ()
