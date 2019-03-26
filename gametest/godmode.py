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
    

if __name__ == "__main__":
  GodModeTest ().main ()
