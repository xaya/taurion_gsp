#!/usr/bin/env python
# coding=utf8

from pxtest import PXTest

"""
Runs tests about the basic handling of characters (creating them, transferring
them and retrieving them through RPC).
"""


class CharactersTest (PXTest):

  def expectPartial (self, expected):
    """
    Expects that the list of characters in the current game state matches
    the given partial values.  chars must be a dictionary indexed by the
    character names.  The values should be dictionaries that are verified
    against the game state; but only the keys given in the expected value
    are compared.  Extra keys in the actual state are ignored.
    """

    chars = self.getCharacters ()
    self.assertEqual (len (chars), len (expected))

    for nm, c in chars.iteritems ():
      assert nm in expected
      c.expectPartial (expected[nm])

  def run (self):
    self.generate (101);

    self.mainLogger.info ("Creating first character...")
    self.createCharacter ("domob", "adam", "r")
    self.sendMove ("", {"nc": {"name": "eve", "faction": "r"}})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob", "faction": "r"},
    })

    self.mainLogger.info ("Already existing name cannot be recreated...")
    self.createCharacter ("", "adam", "g")
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob"},
    })

    self.mainLogger.info ("Testing \"\" as owner name...")
    self.createCharacter ("", "eve", "g")
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob", "faction": "r"},
      "eve": {"owner": "", "faction": "g"},
    })

    self.mainLogger.info ("Creating second character for domob...")
    self.createCharacter ("domob", "foo", "b")
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob", "faction": "r"},
      "eve": {"owner": "", "faction": "g"},
      "foo": {"owner": "domob", "faction": "b"},
    })

    self.mainLogger.info ("Testing Unicode names...")
    self.createCharacter (u"ß", u"äöü", "b")
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob"},
      "eve": {"owner": ""},
      "foo": {"owner": "domob"},
      u"äöü": {"owner": u"ß"},
    })

    self.mainLogger.info ("Transfering a character...")
    c = self.getCharacters ()["adam"]
    c.sendMove ({"send": "andy"})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "andy"},
      "eve": {"owner": ""},
      "foo": {"owner": "domob"},
      u"äöü": {"owner": u"ß"},
    })
    c = self.getCharacters ()["adam"]
    c.sendMove ({"send": ""})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": ""},
      "eve": {"owner": ""},
      "foo": {"owner": "domob"},
      u"äöü": {"owner": u"ß"},
    })

    self.mainLogger.info ("Non-owner cannot update the character...")
    c = self.getCharacters ()["adam"]
    idStr = "%d" % c.getId ()
    self.sendMove ("domob", {"c": {idStr: {"send": "domob"}}})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": ""},
      "eve": {"owner": ""},
      "foo": {"owner": "domob"},
      u"äöü": {"owner": u"ß"},
    })

    self.testReorg ()

  def testReorg (self):
    """
    Reorgs away all the current chain, builds up a small alternate chain
    and then reorgs back to the original chain.  Verifies that the game state
    stays the same.
    """

    self.mainLogger.info ("Testing a reorg...")
    originalState = self.getGameState ()

    blk = self.rpc.xaya.getblockhash (1)
    self.rpc.xaya.invalidateblock (blk)

    self.generate (101)
    self.createCharacter ("domob", "alt", "b")
    self.generate (1)
    self.expectPartial ({
      "alt": {"owner": "domob"},
    })

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)



if __name__ == "__main__":
  CharactersTest ().main ()
