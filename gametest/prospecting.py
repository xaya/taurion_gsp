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
    for i in range (10):
      self.createCharacter ("attacker %d" % i, "g")
    self.generate (1)

    # Set up known positions of the characters.  We use one of the attackers
    # as origin and move all others there.  The target will be moved to a point
    # nearby (but not in range yet).
    c = self.getCharacters ()["attacker 0"]
    self.offset = c.getPosition ()
    movements = {
      "target": offsetCoord ({"x": 20, "y": 0}, self.offset, False),
    }
    for i in range (1, 10):
      movements["attacker %d" % i] = self.offset
    self.moveCharactersTo (movements)

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
    self.assertEqual (region.getId (), self.getRegionAt (self.offset).getId ())
    self.getCharacters ()["target"].sendMove ({"wp": [pos]})
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
    self.getCharacters ()["attacker 1"].sendMove ({"prospect": {}})
    self.generate (1)
    self.getCharacters ()["attacker 2"].sendMove ({"prospect": {}})
    self.generate (1)

    region = self.getRegionAt (self.offset)
    chars = self.getCharacters ()
    self.assertEqual (chars["attacker 1"].getBusy (), {
      "blocks": 9,
      "operation": "prospecting",
      "region": region.getId (),
    })
    self.assertEqual (chars["attacker 2"].getBusy (), None)
    self.assertEqual (region.data["prospection"], {
      "inprogress": chars["attacker 1"].getId ()
    })

    self.generate (9)
    region = self.getRegionAt (self.offset)
    self.assertEqual (self.getCharacters ()["attacker 1"].getBusy (), None)
    self.assertEqual (region.data["prospection"], {"name": "attacker 1"})

    # Now that the region is already prospected, further attempts should
    # just be ignored.
    self.mainLogger.info ("Trying in already prospected region...")
    self.getCharacters ()["attacker 2"].sendMove ({"prospect": {}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["attacker 2"].getBusy (), None)
    region = self.getRegionAt (self.offset)
    self.assertEqual (region.data["prospection"], {"name": "attacker 1"})

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
    self.getCharacters ()["attacker 2"].sendMove ({"prospect": {}})
    self.generate (1)
    region = self.getRegionAt (self.offset)
    c = self.getCharacters ()["attacker 2"]
    self.assertEqual (c.getBusy (), {
      "blocks": 10,
      "operation": "prospecting",
      "region": region.getId (),
    })
    self.assertEqual (region.data["prospection"], {"inprogress": c.getId ()})

    self.generate (10)
    region = self.getRegionAt (self.offset)
    self.assertEqual (self.getCharacters ()["attacker 2"].getBusy (), None)
    self.assertEqual (region.data["prospection"], {"name": "attacker 2"})

    self.rpc.xaya.reconsiderblock (self.reorgBlock)
    self.expectGameState (originalState)


if __name__ == "__main__":
  ProspectingTest ().main ()
