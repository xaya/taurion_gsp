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
Tests the findpath RPC command.
"""


class FindPathTest (PXTest):

  def call (self, source, target, l1range, wpdist):
    return self.rpc.game.findpath (source=source, target=target,
                                   l1range=l1range, wpdist=wpdist)

  def run (self):
    findpath = self.rpc.game.findpath

    # Pair of coordinates that are next to each other, but where one
    # is an obstacle.
    obstacle = {"x": 0, "y": 505}
    passable = {"x": 0, "y": 504}

    # Two coordinates between which a direct path is passable.
    a = {"x": 0, "y": 1}
    b = {"x": 3, "y": 1}

    # Verify exceptions for invalid arguments.
    self.expectError (-1, "source is not a valid coordinate",
                      findpath, source={}, target=a, l1range=10, wpdist=1)
    self.expectError (-1, "target is not a valid coordinate",
                      findpath, source=a, target={}, l1range=10, wpdist=1)
    self.expectError (-1, "l1range is out of bounds",
                      findpath, source=a, target=a, l1range=-1, wpdist=1)
    self.expectError (-1, "wpdist is out of bounds",
                      findpath, source=a, target=a, l1range=1, wpdist=0)

    # Paths that yield no connection.
    self.expectError (1, "no connection",
                      findpath, source=obstacle, target=passable,
                      l1range=10, wpdist=1)
    self.expectError (1, "no connection",
                      findpath, source=passable, target=obstacle,
                      l1range=10, wpdist=1)
    self.expectError (1, "no connection",
                      findpath, source=a, target=b, l1range=1, wpdist=1)
    outOfMap = {"x": 10000, "y": 0}
    self.expectError (1, "no connection",
                      findpath, source=outOfMap, target=outOfMap,
                      l1range=10, wpdist=1)

    # Basic path that is fine with wpdist=1 (every coordinate).
    self.assertEqual (self.call (a, b, l1range=10, wpdist=1),
      {
        "dist": 3000,
        "wp": [a, {"x": 1, "y": 1}, {"x": 2, "y": 1}, b],
      })

    # Path with wpdist=2 (one intermediate point expected).
    self.assertEqual (self.call (a, b, l1range=10, wpdist=2),
      {
        "dist": 3000,
        "wp": [a, {"x": 2, "y": 1}, b],
      })

    # With large wpdist, we expect to get at least start and end.
    self.assertEqual (self.call (a, b, l1range=10, wpdist=10),
      {
        "dist": 3000,
        "wp": [a, b],
      })

    # Test a path with source=target.
    self.assertEqual (self.call (a, a, l1range=0, wpdist=1),
      {
        "dist": 0,
        "wp": [a],
      })


if __name__ == "__main__":
  FindPathTest ().main ()
