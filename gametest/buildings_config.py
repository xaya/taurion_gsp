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
Tests the owner configuration of buildings.
"""

from pxtest import PXTest


class BuildingsConfigTest (PXTest):

  def expectConfig (self, bId, dexFee, serviceFee):
    """
    Checks that the configuration of the given building matches
    the expected values.  None values mean that the field should not
    be explicitly set in the building config.
    """

    expected = {}
    if dexFee is not None:
      expected["dexfee"] = dexFee
    if serviceFee is not None:
      expected["servicefee"] = serviceFee

    self.assertEqual (self.getBuildings ()[bId].data["config"], expected)

  def expectScheduled (self, bId, expected):
    """
    Checks that the scheduled ongoing operations for building updates
    match exactly the expected list.  All entries that are building updates
    must be for the given building.  expected is a list of pairs
    (height offset, new config), where height offset is the end height
    of the operation relative to current block height.
    """

    height = self.rpc.xaya.getblockcount ()
    ongoings = self.getRpc ("getongoings")
    actual = []
    for o in ongoings:
      if o["operation"] != "config":
        continue
      self.assertEqual (o["buildingid"], bId)
      assert "characterid" not in o
      actual.append ((
        o["end_height"] - height,
        o["newconfig"],
      ))

    self.assertEqual (actual, expected)

  def run (self):
    self.collectPremine ()

    self.initAccount ("domob", "r")
    self.initAccount ("andy", "r")
    self.generate (1)
    self.build ("huesli", None, {"x": 0, "y": 0}, rot=0)
    self.build ("huesli", "domob", {"x": 1, "y": 0}, rot=0)
    buildings = self.getBuildings ()
    lastId = max (buildings.keys ())
    unowned = lastId - 1
    domob = lastId
    self.assertEqual (buildings[unowned].getOwner (), None)
    self.assertEqual (buildings[domob].getOwner (), "domob")

    # These operations are invalid.
    self.sendMove ("andy", {"b": [
      {"id": unowned, "sf": 1},
      {"id": domob, "sf": 1},
    ]})
    buildings[domob].sendMove ({"sf": -10})
    self.generate (1)

    self.expectConfig (unowned, None, None)
    self.expectConfig (domob, None, None)
    self.expectScheduled (domob, [])

    self.getBuildings ()[domob].sendMove ({"xf": 25, "sf": 1})
    self.generate (1)
    self.getBuildings ()[domob].sendMove ({})
    self.generate (1)
    self.getBuildings ()[domob].sendMove ({"xf": 50})
    self.generate (1)
    self.getBuildings ()[domob].sendMove ({"sf": 2})
    self.getBuildings ()[domob].sendMove ({"sf": 3})
    self.generate (1)

    self.expectConfig (unowned, None, None)
    self.expectConfig (domob, None, None)
    self.expectScheduled (domob, [
      (7, {"dexfee": 0.25, "servicefee": 1}),
      (9, {"dexfee": 0.5}),
      (10, {"servicefee": 2}),
      (10, {"servicefee": 3}),
    ])

    self.generate (6)
    self.expectConfig (unowned, None, None)
    self.expectConfig (domob, None, None)
    self.expectScheduled (domob, [
      (1, {"dexfee": 0.25, "servicefee": 1}),
      (3, {"dexfee": 0.5}),
      (4, {"servicefee": 2}),
      (4, {"servicefee": 3}),
    ])

    self.generate (1)
    self.expectConfig (unowned, None, None)
    self.expectConfig (domob, dexFee=0.25, serviceFee=1)
    self.expectScheduled (domob, [
      (2, {"dexfee": 0.5}),
      (3, {"servicefee": 2}),
      (3, {"servicefee": 3}),
    ])

    self.generate (2)
    self.expectConfig (unowned, None, None)
    self.expectConfig (domob, dexFee=0.5, serviceFee=1)
    self.expectScheduled (domob, [
      (1, {"servicefee": 2}),
      (1, {"servicefee": 3}),
    ])

    self.generate (1)
    self.expectConfig (unowned, None, None)
    self.expectConfig (domob, dexFee=0.5, serviceFee=3)
    self.expectScheduled (domob, [])


if __name__ == "__main__":
  BuildingsConfigTest ().main ()
