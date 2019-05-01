#!/usr/bin/env python

from pxtest import PXTest

"""
Tests the god-mode commands.
"""


class GodModeTest (PXTest):

  def run (self):
    self.generate (101);

    self.createCharacter ("domob", "r")
    self.generate (1)
    c = self.getCharacters ()["domob"]
    pos = c.getPosition ()
    idStr = c.getIdStr ()

    self.mainLogger.info ("Testing teleport...")
    target = {"x": 28, "y": 9}
    assert pos != target
    self.adminCommand ({"god": {"teleport": {idStr: target}}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["domob"].getPosition (), target)

    self.mainLogger.info ("Testing sethp...")
    self.adminCommand ({
      "god":
        {
          "sethp":
            {
              idStr: {"a": 32, "s": 15, "ma": 100, "ms": 90},
            },
        },
    })
    self.generate (1)
    hp = self.getCharacters ()["domob"].data["combat"]["hp"]
    self.assertEqual (hp["current"], {"armour": 32, "shield": 15})
    self.assertEqual (hp["max"], {"armour": 100, "shield": 90})
    

if __name__ == "__main__":
  GodModeTest ().main ()
