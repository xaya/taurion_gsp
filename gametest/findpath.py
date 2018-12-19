#!/usr/bin/env python

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

    # Basic path that is fine with wpdist=1 (every coordinate).
    self.assertEqual (self.call (a, b, l1range=10, wpdist=1),
      {
        "dist": 3,
        "wp": [a, {"x": 1, "y": 1}, {"x": 2, "y": 1}, b],
      })

    # Path with wpdist=2 (one intermediate point expected).
    self.assertEqual (self.call (a, b, l1range=10, wpdist=2),
      {
        "dist": 3,
        "wp": [a, {"x": 2, "y": 1}, b],
      })

    # With large wpdist, we expect to get at least start and end.
    self.assertEqual (self.call (a, b, l1range=10, wpdist=10),
      {
        "dist": 3,
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
