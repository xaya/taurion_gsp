#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2025  Autonomous Worlds Ltd
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

from pending import sleepSome


class BasicProspectingTest (PXTest):

  def expectProspectedBy (self, pos, name):
    """
    Asserts that the region at the given position is marked as having been
    prospected by the given name.  Eventual prizes won will be ignored.
    """

    region = self.getRegionAt (pos)
    self.assertEqual (region.data["prospection"]["name"], name)

  def run (self):
    self.mainLogger.info ("Setting up test characters...")
    self.initAccount ("target", "r")
    self.createCharacters ("target")
    self.initAccount ("attacker 1", "g")
    self.createCharacters ("attacker 1")
    self.initAccount ("attacker 2", "g")
    self.createCharacters ("attacker 2")
    self.generate (1)
    self.changeCharacterVehicle ("attacker 1", "light attacker")
    self.changeCharacterVehicle ("attacker 2", "light attacker")

    # Set up known positions of the characters.  We use a known good position
    # as origin and move all attackers there.  The target will be moved to a
    # point nearby (but not in range yet).
    self.offset = {"x": -1050, "y": 1272}
    self.moveCharactersTo ({
      "target": offsetCoord ({"x": 20, "y": 0}, self.offset, False),
      "attacker 1": offsetCoord ({"x": 0, "y": 1}, self.offset, False),
      "attacker 2": offsetCoord ({"x": -1, "y": 0}, self.offset, False),
    })

    # Move character and start prospecting.  This should stop the movement.
    # Further movements should be ignored.  Verify the prospection effects.
    self.mainLogger.info ("Basic prospecting and movement...")

    self.getCharacters ()["target"].moveTowards (self.offset)
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
    })
    self.assertEqual (region.data["prospection"], {"inprogress": c.getId ()})

    c.moveTowards (self.offset)
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
    self.assertEqual (region.data["prospection"]["name"], "target")
    _, height = self.env.getChainTip ()
    self.assertEqual (region.data["prospection"]["height"], height)

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

    self.snapshot = self.env.snapshot ()
    self.getCharacters ()[self.prospectors[0]].sendMove ({"prospect": {}})
    self.generate (1)
    self.getCharacters ()[self.prospectors[1]].sendMove ({"prospect": {}})
    self.generate (1)

    region = self.getRegionAt (self.offset)
    chars = self.getCharacters ()
    self.assertEqual (chars[self.prospectors[0]].getBusy (), {
      "blocks": 9,
      "operation": "prospecting",
    })
    self.assertEqual (chars[self.prospectors[1]].getBusy (), None)
    self.assertEqual (region.data["prospection"], {
      "inprogress": chars[self.prospectors[0]].getId ()
    })

    self.generate (9)
    self.assertEqual (self.getCharacters ()[self.prospectors[0]].getBusy (),
                      None)
    self.expectProspectedBy (self.offset, self.prospectors[0])

    # Now that the region is already prospected, further (immediate) attempts
    # should just be ignored.
    self.mainLogger.info ("Trying in already prospected region...")
    self.getCharacters ()[self.prospectors[1]].sendMove ({"prospect": {}})
    sleepSome ()
    self.assertEqual (self.getPendingState ()["characters"], [])
    self.generate (1)
    self.assertEqual (self.getCharacters ()[self.prospectors[1]].getBusy (),
                      None)
    self.expectProspectedBy (self.offset, self.prospectors[0])

    # Mine all the resources (and drop them), so that we can reprospect
    # the region in the next step.
    self.mainLogger.info ("Mining all resources...")
    miner = self.prospectors[1]
    while True:
      typ, remaining = self.getRegionAt (self.offset).getResource ()
      if remaining == 0:
        break
      self.getCharacters ()[miner].sendMove ({
        "drop": {"f": {typ: 1000}},
        "mine": {},
      })
      self.generate (50)

    # Start prospecting after the expiry, but kill the prospector.  This should
    # effectively "unprospect" the region completely.
    self.mainLogger.info ("Attempting re-prospecting...")
    self.generate (100)
    self.createCharacters ("target", 1)
    self.generate (1)
    self.setCharactersHP ({
      "target": {"a": 1000, "ma": 1000, "s": 0, "ms": 100},
    })
    self.moveCharactersTo ({
      "target": self.offset,
    })
    r = self.getRegionAt (self.offset)
    c = self.getCharacters ()["target"]
    c.sendMove ({"prospect": {}})
    sleepSome ()
    self.assertEqual (self.getPendingState ()["characters"], [
      {
        "id": c.getId (),
        "drop": False,
        "pickup": False,
        "prospecting": r.getId (),
      },
    ])
    self.generate (1)
    self.assertEqual (self.getCharacters ()["target"].getBusy ()["operation"],
                      "prospecting")

    self.setCharactersHP ({
      "target": {"a": 1, "s": 0},
    })
    self.generate (1)
    assert "target" not in self.getCharacters ()
    r = self.getRegionAt (self.offset)
    assert "prospection" not  in r.data
    self.assertEqual (r.getResource (), None)

    # Actually re-prospect the region.
    self.mainLogger.info ("Finish reprospecting...")
    self.initAccount ("reprospector", "g")
    self.createCharacters ("reprospector", 1)
    self.generate (1)
    self.moveCharactersTo ({
      "reprospector": self.offset,
    })
    self.getCharacters ()["reprospector"].sendMove ({"prospect": {}})
    self.generate (15)
    self.expectProspectedBy (self.offset, "reprospector")
    assert self.getRegionAt (self.offset).getResource () is not None

    self.testReorg ()

  def testReorg (self):
    """
    Test basic reorg handling, where another character prospects a
    region on the fork.
    """

    self.mainLogger.info ("Testing a reorg...")

    self.snapshot.restore ()
    self.getCharacters ()[self.prospectors[1]].sendMove ({"prospect": {}})
    self.generate (1)
    region = self.getRegionAt (self.offset)
    c = self.getCharacters ()[self.prospectors[1]]
    self.assertEqual (c.getBusy (), {
      "blocks": 10,
      "operation": "prospecting",
    })
    self.assertEqual (region.data["prospection"], {"inprogress": c.getId ()})

    self.generate (10)
    self.assertEqual (self.getCharacters ()[self.prospectors[1]].getBusy (),
                      None)
    self.expectProspectedBy (self.offset, self.prospectors[1])


if __name__ == "__main__":
  BasicProspectingTest ().main ()
