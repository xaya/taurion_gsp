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

import json
import threading
import time


# A thread that does an async call to findpath (plus setting data
# first with setpathdata for the call).
class AsyncFindPath (threading.Thread):

  def __init__ (self, gameNode, buildings, characters, *args, **kwargs):
    super ().__init__ ()
    self.rpc = gameNode.createRpc ()
    self.args = args
    self.kwargs = kwargs

    self.result = None
    self.rpc.setpathdata (buildings=buildings, characters=characters)
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

  def call (self, source, target, l1range, faction="r", exbuildings=[]):
    return self.rpc.game.findpath (source=source, target=target,
                                   faction=faction, l1range=l1range,
                                   exbuildings=exbuildings)

  def strip (self, val):
    """
    Strips a findpath result (in particular, removes the "encoded" path),
    so that it can easily be compared against golden data.
    """

    assert "encoded" in val
    del val["encoded"]

    return val

  def run (self):
    self.collectPremine ()

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
                      self.call, source={}, target=a, l1range=10)
    self.expectError (-1, "target is not a valid coordinate",
                      self.call, source=a, target={}, l1range=10)
    self.expectError (-1, "l1range is out of bounds",
                      self.call, source=a, target=a, l1range=-1)
    for f in ["a", "invalid"]:
      self.expectError (-1, "faction is invalid",
                        self.call, source=a, target=a, faction=f, l1range=1)

    # Also briefly test invalid arguments to encodewaypoints (which is
    # related to findpath).  The proper functionality is tested in other
    # tests, e.g. movement.py.
    self.expectError (-32602, ".*Invalid method parameters.*",
                      self.rpc.game.encodewaypoints, wp={})
    self.expectError (-1, "invalid waypoints",
                      self.rpc.game.encodewaypoints, wp=["invalid"])

    # Paths that yield no connection.
    self.expectError (1, "no connection",
                      self.call, source=passable, target=obstacle, l1range=10)
    self.expectError (1, "no connection",
                      self.call, source=a, target=b, l1range=1)
    outOfMap = {"x": 10000, "y": 0}
    self.expectError (1, "no connection",
                      self.call, source=outOfMap, target=outOfMap, l1range=10)

    # Basic path that is fine and along a single principal direction.
    self.assertEqual (self.strip (self.call (a, b, l1range=10)),
      {
        "dist": 3000,
        "wp": [a, b],
      })

    # Path which needs a waypoint (because there is no principal direction
    # between start and end).
    self.assertEqual (self.strip (self.call (a, c, l1range=10)),
      {
        "dist": 4000,
        "wp": [a, b, c],
      })

    # Test a path with source=target.
    self.assertEqual (self.strip (self.call (a, a, l1range=0)),
      {
        "dist": 0,
        "wp": [a],
      })

    self.testExbuildings ()
    self.testInvalidData ()
    self.testWithData ()

  def testExbuildings (self):
    self.mainLogger.info ("Testing exbuildings...")

    # Invalid exbuildings arguments.
    a = {"x": -10, "y": 0}
    b = {"x": 10, "y": 0}
    self.expectError (-32602, ".*Invalid method parameters.*",
                      self.call, source=a, target=b, l1range=100,
                      exbuildings=42)
    for exb in [[0], ["foo"], [5, -42]]:
      self.expectError (-1, "exbuildings is not valid",
                        self.call, source=a, target=b, l1range=100,
                        exbuildings=exb)

    # Apply exbuildings to path to a building.
    self.build ("checkmark", None, b, rot=0)
    buildings = self.getRpc ("getbuildings")
    bId = buildings[-1]["id"]
    self.rpc.game.setpathdata (buildings=buildings, characters=[])
    self.expectError (1, "no connection",
                      self.call, source=a, target=b, l1range=100)
    self.assertEqual (self.call (source=a, target=b, l1range=100,
                                 exbuildings=[bId, bId, 123456])["dist"],
                      20 * 1000)

  def testInvalidData (self):
    self.mainLogger.info ("Testing invalid setpathdata...")

    self.expectError (-32602, ".*Invalid method parameters.*",
                      self.rpc.game.setpathdata, buildings={}, characters=[])
    self.expectError (-32602, ".*Invalid method parameters.*",
                      self.rpc.game.setpathdata, buildings=[], characters={})

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
                        self.rpc.game.setpathdata,
                        buildings=specs, characters=[])

    for specs in [
      [42],
      ["foo"],
      [{}],
      [{"faction": "r"}],
      [{"position": coord}],
      [{"faction": 42, "position": coord}],
      [{"faction": "a", "position": coord}],
      [{"faction": "a", "position": coord}],
      [{"faction": "r", "position": "foo"}],
      [{"faction": "r", "position": {"x": 1}}],
      [
        {"faction": "r", "position": coord},
        {"faction": "r", "position": coord},
      ],
    ]:
      self.expectError (-1, "characters is invalid",
                        self.rpc.game.setpathdata,
                        buildings=[], characters=specs)

  def testWithData (self):
    self.mainLogger.info ("Testing with set data...")

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
      "exbuildings": [],
    }
    before = time.clock_gettime (time.CLOCK_MONOTONIC)
    path = self.call (**kwargs)
    after = time.clock_gettime (time.CLOCK_MONOTONIC)
    baseDuration = after - before
    self.log.info ("Duration for single call: %.3f s" % baseDuration)
    baseLen = path["dist"]

    # This is a very long path.  Make sure its encoded form is relatively
    # small, though.
    assert len (path["wp"]) > 100
    assert len (path["encoded"]) < 750
    serialised = json.dumps (path["wp"], separators=(",", ":"))
    assert len (serialised) > 3000

    # Create some test characters that we can use as obstacles.
    self.initAccount ("red", "r")
    self.initAccount ("green", "g")
    self.generate (1)
    self.createCharacters ("red", 3)
    self.createCharacters ("green")

    # Now place buildings and characters in two steps on the map, which make
    # the path from longA to longB further.  We use the outputs of getbuildings
    # and getcharacters itself, to ensure that it can be passed directly back
    # to setpathdata.
    buildings = [[]]
    characters = [[]]

    self.build ("r rt", None,
                offsetCoord (longA, {"x": 1, "y": -1}, False), rot=0)
    self.build ("r rt", None,
                offsetCoord (longA, {"x": 0, "y": 1}, False), rot=0)
    self.moveCharactersTo ({
      "red": longA,
      "red 2": offsetCoord (longA, {"x": 1, "y": 0}, False),
    })
    bIds = [
      b["id"]
      for b in self.getRpc ("getbuildings")
      if b["type"] == "r rt"
    ]
    self.getCharacters ()["red"].sendMove ({"eb": bIds[0]})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["red"].isInBuilding (), True)

    buildings.append (self.getRpc ("getbuildings"))
    characters.append (self.getRpc ("getcharacters"))

    self.build ("r rt", None,
                offsetCoord (longA, {"x": 0, "y": -1}, False), rot=0)
    self.moveCharactersTo ({
      "red 3": offsetCoord (longA, {"x": -1, "y": 1}, False),
      "green": offsetCoord (longA, {"x": -1, "y": 0}, False),
    })

    buildings.append (self.getRpc ("getbuildings"))
    characters.append (self.getRpc ("getcharacters"))

    # We do three calls now in parallel, with different sets of buildings.
    # All should be running concurrently and not block each other.  The total
    # time should be shorter than sequential execution.
    before = time.clock_gettime (time.CLOCK_MONOTONIC)
    calls = []
    for b, c in zip (buildings, characters):
      calls.append (AsyncFindPath (self.gamenode, b, c, **kwargs))
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
