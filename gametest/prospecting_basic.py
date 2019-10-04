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
Tests basic rules for prospecting with characters and various interactions of
that with movement and combat.
"""

from pxtest import PXTest, offsetCoord


class BasicProspectingTest (PXTest):

  def expectProspectedBy (self, pos, name):
    """
    Asserts that the region at the given position is marked as having been
    prospected by the given name.  Eventual prizes won will be ignored.
    """

    region = self.getRegionAt (pos)
    data = region.data["prospection"]
    if "prize" in data:
      del data["prize"]
    self.assertEqual (data, {"name": name})

  def run (self):
    self.collectPremine ()

    self.mainLogger.info ("Setting up test characters...")
    self.initAccount ("target", "r")
    self.createCharacters ("target")
    self.initAccount ("attacker 1", "g")
    self.createCharacters ("attacker 1")
    self.initAccount ("attacker 2", "g")
    self.createCharacters ("attacker 2")
    self.generate (1)

    # Set up known positions of the characters.  We use a known good position
    # as origin and move all attackers there.  The target will be moved to a
    # point nearby (but not in range yet).
    self.offset = {"x": -1050, "y": 1272}
    self.moveCharactersTo ({
      "target": offsetCoord ({"x": 20, "y": 0}, self.offset, False),
      "attacker 1": offsetCoord ({"x": 0, "y": 0}, self.offset, False),
      "attacker 2": offsetCoord ({"x": -1, "y": 0}, self.offset, False),
    })

    # Move character and start prospecting.  This should stop the movement.
    # Further movements should be ignored.  Verify the prospection effects.
    self.mainLogger.info ("Basic prospecting and movement...")

    self.getCharacters ()["target"].sendMove ({"wp": [self.offset]})
    self.generate (2)
    c = self.getCharacters ()["target"]
    pos = c.getPosition ()
    assert c.isMoving ()
    region = self.getRegionAt (pos)
    assert "prospection" not in region.data
    assert region.getId () != self.getRegionAt (self.offset).getId ()

    c.sendMove ({"prospect": {}})
    self.generate (1)
    c = self.getCharacters ()["target"]
    self.assertEqual (c.getPosition (), pos)
    region = self.getRegionAt (pos)
    assert not c.isMoving ()
    self.assertEqual (c.getBusy (), {
      "blocks": 10,
      "operation": "prospecting",
      "region": region.getId (),
    })
    self.assertEqual (region.data["prospection"], {"inprogress": c.getId ()})

    c.sendMove ({"wp": [self.offset]})
    self.generate (1)
    c = self.getCharacters ()["target"]
    self.assertEqual (c.getPosition (), pos)
    assert not c.isMoving ()
    self.assertEqual (c.getBusy ()["blocks"], 9)

    self.generate (9)
    c = self.getCharacters ()["target"]
    self.assertEqual (c.getPosition (), pos)
    self.assertEqual (c.getBusy (), None)
    self.expectProspectedBy (pos, "target")

    # Move towards attackers and prospect there, but have the character
    # killed before it is done.
    self.mainLogger.info ("Killing prospecting character...")

    pos = offsetCoord ({"x": 5, "y": 0}, self.offset, False)
    region = self.getRegionAt (pos)

    self.prospectors = ["attacker 1", "attacker 2"]
    char = self.getCharacters ()
    for p in self.prospectors:
      prospRegion = self.getRegionAt (char[p].getPosition ())
      self.assertEqual (region.getId (), prospRegion.getId ())

    self.moveCharactersTo ({
      "target": pos
    })
    self.generate (1)

    c = self.getCharacters ()["target"]
    self.assertEqual (c.getPosition (), pos)
    c.sendMove ({"prospect": {}})
    self.generate (1)
    c = self.getCharacters ()["target"]
    self.assertEqual (c.getBusy (), {
      "blocks": 10,
      "operation": "prospecting",
      "region": region.getId (),
    })

    self.setCharactersHP ({
      "target": {"a": 1, "s": 0},
    })
    self.generate (1)
    assert "target" not in self.getCharacters ()
    region = self.getRegionAt (pos)
    assert "prospection" not in region.data

    # Start prospecting with one of the attackers and later with another one.
    # Check that the first goes through and the second is ignored.
    self.mainLogger.info ("Competing prospectors...")

    self.reorgBlock = self.rpc.xaya.getbestblockhash ()
    self.getCharacters ()[self.prospectors[0]].sendMove ({"prospect": {}})
    self.generate (1)
    self.getCharacters ()[self.prospectors[1]].sendMove ({"prospect": {}})
    self.generate (1)

    region = self.getRegionAt (self.offset)
    chars = self.getCharacters ()
    self.assertEqual (chars[self.prospectors[0]].getBusy (), {
      "blocks": 9,
      "operation": "prospecting",
      "region": region.getId (),
    })
    self.assertEqual (chars[self.prospectors[1]].getBusy (), None)
    self.assertEqual (region.data["prospection"], {
      "inprogress": chars[self.prospectors[0]].getId ()
    })

    self.generate (9)
    self.assertEqual (self.getCharacters ()[self.prospectors[0]].getBusy (),
                      None)
    self.expectProspectedBy (self.offset, self.prospectors[0])

    # Now that the region is already prospected, further attempts should
    # just be ignored.
    self.mainLogger.info ("Trying in already prospected region...")
    self.getCharacters ()[self.prospectors[1]].sendMove ({"prospect": {}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()[self.prospectors[1]].getBusy (),
                      None)
    self.expectProspectedBy (self.offset, self.prospectors[0])

    self.testReorg ()

  def testReorg (self):
    """
    Test basic reorg handling, where another character prospects a
    region on the fork.
    """

    self.mainLogger.info ("Testing a reorg...")
    bestBlk = self.rpc.xaya.getbestblockhash ()
    originalState = self.getGameState ()

    self.rpc.xaya.invalidateblock (self.reorgBlock)
    self.getCharacters ()[self.prospectors[1]].sendMove ({"prospect": {}})
    self.generate (1)
    region = self.getRegionAt (self.offset)
    c = self.getCharacters ()[self.prospectors[1]]
    self.assertEqual (c.getBusy (), {
      "blocks": 10,
      "operation": "prospecting",
      "region": region.getId (),
    })
    self.assertEqual (region.data["prospection"], {"inprogress": c.getId ()})

    self.generate (10)
    self.assertEqual (self.getCharacters ()[self.prospectors[1]].getBusy (),
                      None)
    self.expectProspectedBy (self.offset, self.prospectors[1])

    self.rpc.xaya.reconsiderblock (self.reorgBlock)
    self.assertEqual (self.rpc.xaya.getbestblockhash (), bestBlk)
    self.expectGameState (originalState)


if __name__ == "__main__":
  BasicProspectingTest ().main ()
