#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2020  Autonomous Worlds Ltd
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
Tests the findpath RPC command.
"""

from pxtest import PXTest, offsetCoord

import threading
import time


# A thread that does an async call to findpath (plus setting buildings
# first with setpathbuildings for the call).
class AsyncFindPath (threading.Thread):

  def __init__ (self, gameNode, buildings, *args, **kwargs):
    super ().__init__ ()
    self.rpc = gameNode.createRpc ()
    self.args = args
    self.kwargs = kwargs

    self.result = None
    self.rpc.setpathbuildings (buildings=buildings)
    self.start ()

    # Wait a bit so that the findpath call has (likely) started already
    # and "locked in" its copy of the buildings.
    time.sleep (0.01)

  def run (self):
    self.result = self.rpc.findpath (*self.args, **self.kwargs)

  def finish (self):
    self.join ()
    return self.result


class FindPathTest (PXTest):

  def call (self, source, target, l1range, faction="r"):
    return self.rpc.game.findpath (source=source, target=target,
                                   faction=faction, l1range=l1range)

  def run (self):
    self.collectPremine ()

    findpath = self.rpc.game.findpath

    # Pair of coordinates that are next to each other, but where one
    # is an obstacle.
    obstacle = {"x": 0, "y": 505}
    passable = {"x": 0, "y": 504}

    # Coordinates between which paths are possible.
    a = {"x": 0, "y": 1}
    b = {"x": 3, "y": 1}
    c = {"x": 3, "y": 2}

    # Verify exceptions for invalid arguments.
    self.expectError (-1, "source is not a valid coordinate",
                      findpath, source={}, target=a, faction="r", l1range=10)
    self.expectError (-1, "target is not a valid coordinate",
                      findpath, source=a, target={}, faction="r", l1range=10)
    self.expectError (-1, "l1range is out of bounds",
                      findpath, source=a, target=a, faction="r", l1range=-1)
    for f in ["a", "invalid"]:
      self.expectError (-1, "faction is invalid",
                        findpath, source=a, target=a, faction=f, l1range=1)

    # Paths that yield no connection.
    self.expectError (1, "no connection",
                      findpath, source=passable, target=obstacle, faction="r",
                      l1range=10)
    self.expectError (1, "no connection",
                      findpath, source=a, target=b, faction="r", l1range=1)
    outOfMap = {"x": 10000, "y": 0}
    self.expectError (1, "no connection",
                      findpath, source=outOfMap, target=outOfMap, faction="r",
                      l1range=10)

    # Basic path that is fine and along a single principal direction.
    self.assertEqual (self.call (a, b, l1range=10),
      {
        "dist": 3000,
        "wp": [a, b],
      })

    # Path which needs a waypoint (because there is no principal direction
    # between start and end).
    self.assertEqual (self.call (a, c, l1range=10),
      {
        "dist": 4000,
        "wp": [a, b, c],
      })

    # Test a path with source=target.
    self.assertEqual (self.call (a, a, l1range=0),
      {
        "dist": 0,
        "wp": [a],
      })

    # Invalid building specs for setpathbuildings.
    self.expectError (-32602, ".*Invalid method parameters.*",
                      self.rpc.game.setpathbuildings, buildings={})
    coord = {"x": 1, "y": 2}
    for specs in [
      [42],
      ["foo"],
      [{}],
      [{"type": "checkmark", "rotationsteps": 0, "centre": coord}],
      [{"id": -5, "type": "checkmark", "rotationsteps": 0, "centre": coord}],
      [{"id": 0, "type": "checkmark", "rotationsteps": 0, "centre": coord}],
      [{"id": 10, "rotationsteps": 0, "centre": coord}],
      [{"id": 10, "type": 42, "rotationsteps": 0, "centre": coord}],
      [{"id": 10, "type": "invalid", "rotationsteps": 0, "centre": coord}],
      [{"id": 10, "type": "checkmark", "centre": coord}],
      [{"id": 10, "type": "checkmark", "rotationsteps": "0", "centre": coord}],
      [{"id": 10, "type": "checkmark", "rotationsteps": -1, "centre": coord}],
      [{"id": 10, "type": "checkmark", "rotationsteps": 6, "centre": coord}],
      [{"id": 10, "type": "checkmark", "rotationsteps": 0}],
      [{"id": 10, "type": "checkmark", "rotationsteps": 0, "centre": "(0, 0)"}],
      [{"id": 10, "type": "checkmark", "rotationsteps": 0, "centre": {"x": 1}}],
      [
        {"id": 10, "type": "checkmark", "rotationsteps": 0, "centre": coord},
        {"id": 10, "type": "checkmark", "rotationsteps": 0, "centre": coord},
      ],
    ]:
      self.expectError (-1, "buildings is invalid",
                        self.rpc.game.setpathbuildings, buildings=specs)

    # This is a very long path, which takes a non-negligible amount of time
    # to compute.  We use this later to ensure that multiple calls are
    # actually done in parallel and not blocking each other.
    longA = {"x": -2000, "y": 0}
    longB = {"x": 2000, "y": 0}
    kwargs = {
      "source": longA,
      "target": longB,
      "faction": "r",
      "l1range": 8000,
    }
    before = time.clock_gettime (time.CLOCK_MONOTONIC)
    baseLen = findpath (**kwargs)["dist"]
    after = time.clock_gettime (time.CLOCK_MONOTONIC)
    baseDuration = after - before
    self.log.info ("Duration for single call: %.3f s" % baseDuration)

    # Now place buildings in two steps on the map, which make the path from
    # longA to longB further.  We use the output of getbuildings itself, to
    # ensure that it can be passed directly back to setpathbuildings.
    buildings = [[]]
    self.build ("r rt", None,
                offsetCoord (longA, {"x": 1, "y": 0}, False), rot=0)
    self.build ("r rt", None,
                offsetCoord (longA, {"x": 1, "y": -1}, False), rot=0)
    self.build ("r rt", None,
                offsetCoord (longA, {"x": 0, "y": 1}, False), rot=0)
    buildings.append (self.getRpc ("getbuildings"))
    self.build ("r rt", None,
                offsetCoord (longA, {"x": 0, "y": -1}, False), rot=0)
    self.build ("r rt", None,
                offsetCoord (longA, {"x": -1, "y": 1}, False), rot=0)
    buildings.append (self.getRpc ("getbuildings"))

    # We do three calls now in parallel, with different sets of buildings.
    # All should be running concurrently and not block each other.  The total
    # time should be shorter than sequential execution.
    before = time.clock_gettime (time.CLOCK_MONOTONIC)
    calls = []
    for b in buildings:
      calls.append (AsyncFindPath (self.gamenode, b, **kwargs))
    [shortLen, midLen, longLen] = [c.finish ()["dist"] for c in calls]
    after = time.clock_gettime (time.CLOCK_MONOTONIC)
    threeDuration = after - before
    self.log.info ("Duration for three calls: %.3f s" % threeDuration)
    self.assertEqual (shortLen, baseLen)
    assert midLen > shortLen
    assert longLen > midLen
    assert threeDuration < 2 * baseDuration


if __name__ == "__main__":
  FindPathTest ().main ()
