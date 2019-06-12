#!/usr/bin/env python

from pxtest import PXTest

"""
Tests the fame and kills update of accounts.
"""


class FameTest (PXTest):

  def run (self):
    self.generate (110);

    self.mainLogger.info ("Characters killing each other at the same time...")
    self.createCharacter ("foo", "r")
    self.createCharacter ("bar", "g")
    self.generate (1)
    self.moveCharactersTo ({
      "foo": {"x": 0, "y": 0},
      "bar": {"x": 0, "y": 0},
    })
    self.setCharactersHP ({
      "foo": {"a": 1, "s": 0},
      "bar": {"a": 1, "s": 0},
    })
    self.generate (1)
    chars = self.getCharacters ()
    assert "foo" not in chars
    assert "bar" not in chars
    accounts = self.getAccounts ()
    self.assertEqual (accounts["foo"].data["kills"], 1)
    self.assertEqual (accounts["foo"].data["fame"], 100)
    self.assertEqual (accounts["bar"].data["kills"], 1)
    self.assertEqual (accounts["bar"].data["fame"], 100)

    self.mainLogger.info ("Multiple killers...")
    self.createCharacters ("red", 2 * ["r"])
    self.createCharacter ("green", "g")
    self.createCharacter ("blue", "b")
    self.generate (1)
    self.moveCharactersTo ({
      "blue": {"x": 0, "y": 0},
      "red": {"x": 10, "y": 0},
      "red 2": {"x": 0, "y": 10},
      "green": {"x": -10, "y": 0},
    })
    self.setCharactersHP ({
      "blue": {"a": 1, "s": 0},
    })
    self.generate (1)
    chars = self.getCharacters ()
    assert "blue" not in chars
    assert "red" in chars
    assert "red 2" in chars
    assert "green" in chars
    accounts = self.getAccounts ()
    self.assertEqual (accounts["red"].data["kills"], 1)
    self.assertEqual (accounts["red"].data["fame"], 150)
    self.assertEqual (accounts["green"].data["kills"], 1)
    self.assertEqual (accounts["green"].data["fame"], 150)
    self.assertEqual (accounts["blue"].data["kills"], 0)
    self.assertEqual (accounts["blue"].data["fame"], 0)

    self.mainLogger.info ("Many characters for a name...")
    armySize = 10
    self.createCharacters ("army", armySize * ["r"])
    self.createCharacter ("target", "b")
    self.generate (1)
    mv = {"target": {"x": 100, "y": 0}}
    for i in range (1, armySize):
      mv["army %d" % (i + 1)] = {"x": 101, "y": i - armySize // 2}
    self.moveCharactersTo (mv)
    self.setCharactersHP ({
      "target": {"a": 1, "s": 0},
    })
    self.generate (1)
    accounts = self.getAccounts ()
    chars = self.getCharacters ()
    assert "target" not in chars
    self.assertEqual (accounts["army"].data["kills"], 1)
    self.assertEqual (accounts["army"].data["fame"], 200)
    self.assertEqual (accounts["target"].data["kills"], 0)
    self.assertEqual (accounts["target"].data["fame"], 0)


if __name__ == "__main__":
  FameTest ().main ()
