#!/usr/bin/env python

from pxtest import PXTest, offsetCoord

"""
Tests prospecting with characters and various interactions of that with
movement and combat.
"""


class ProspectingTest (PXTest):

  def run (self):
    self.generate (110);

    self.mainLogger.info ("Setting up test characters...")
    self.createCharacter ("target", "r")
    self.generate (1)

    # Set up known positions of the characters.  We use a known good position
    # as origin and move all attackers there.  The target will be moved to a
    # point nearby (but not in range yet).
    self.offset = {"x": -1045, "y": 1265}
    blkLower = offsetCoord ({"x": -1, "y": -1}, self.offset, False)
    blkUpper = offsetCoord ({"x": 0, "y": 1}, self.offset, False)
    self.createCharacterBlock ("attacker %d", "g", blkLower, blkUpper)
    self.moveCharactersTo ({
      "target": offsetCoord ({"x": 20, "y": 0}, self.offset, False),
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
    region = self.getRegionAt (pos)
    self.assertEqual (region.data["prospection"], {"name": "target"})

    # Move towards attackers and prospect there, but have the character
    # killed before it is done.
    self.mainLogger.info ("Killing prospecting character...")

    pos = offsetCoord ({"x": 5, "y": 0}, self.offset, False)
    region = self.getRegionAt (pos)

    self.prospectors = ["attacker 3", "attacker 4"]
    char = self.getCharacters ()
    for p in self.prospectors:
      prospRegion = self.getRegionAt (char[p].getPosition ())
      self.assertEqual (region.getId (), prospRegion.getId ())

    self.moveCharactersTo ({
      "target": pos
    })
    self.generate (20)

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

    self.generate (5)
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
    region = self.getRegionAt (self.offset)
    self.assertEqual (self.getCharacters ()[self.prospectors[0]].getBusy (),
                      None)
    self.assertEqual (region.data["prospection"], {"name": self.prospectors[0]})

    # Now that the region is already prospected, further attempts should
    # just be ignored.
    self.mainLogger.info ("Trying in already prospected region...")
    self.getCharacters ()[self.prospectors[1]].sendMove ({"prospect": {}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()[self.prospectors[1]].getBusy (),
                      None)
    region = self.getRegionAt (self.offset)
    self.assertEqual (region.data["prospection"], {"name": self.prospectors[0]})

    # Finally, test a reorg situation.
    self.testReorg ()

  def testReorg (self):
    """
    Test basic reorg handling, where another character prospects a
    region on the fork.
    """

    self.mainLogger.info ("Testing a reorg...")
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
    region = self.getRegionAt (self.offset)
    self.assertEqual (self.getCharacters ()[self.prospectors[1]].getBusy (),
                      None)
    self.assertEqual (region.data["prospection"], {"name": self.prospectors[1]})

    self.rpc.xaya.reconsiderblock (self.reorgBlock)
    self.expectGameState (originalState)


if __name__ == "__main__":
  ProspectingTest ().main ()
