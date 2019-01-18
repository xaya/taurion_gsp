#!/usr/bin/env python
# coding=utf8

from pxtest import PXTest, CHARACTER_COST

"""
Runs tests about the basic handling of characters (creating them, transferring
them and retrieving them through RPC).
"""


class CharactersTest (PXTest):

  def expectPartial (self, chars):
    """
    Expects that the list of characters in the current game state matches
    the given partial values.  chars must be a dictionary indexed by the
    character names.  The values should be dictionaries that are verified
    against the game state; but only the keys given in the expected value
    are compared.  Extra keys in the actual state are ignored.
    """

    state = self.getGameState ()
    assert "characters" in state
    self.assertEqual (len (chars), len (state["characters"]))

    for c in state["characters"]:
      assert "name" in c
      assert c["name"] in chars
      for key, val in chars[c["name"]].iteritems ():
        assert key in c
        self.assertEqual (val, c[key])

  def run (self):
    self.generate (101);

    self.mainLogger.info ("Creating first character...")
    self.moveWithPayment ("domob", {"nc": {"name": "adam"}}, CHARACTER_COST)
    self.sendMove ("", {"nc": {"name": "eve"}})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob"},
    })

    self.mainLogger.info ("Already existing name cannot be recreated...")
    self.moveWithPayment ("", {"nc": {"name": "adam"}}, CHARACTER_COST)
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob"},
    })

    self.mainLogger.info ("Testing \"\" as owner name...")
    self.moveWithPayment ("", {"nc": {"name": "eve"}}, CHARACTER_COST)
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob"},
      "eve": {"owner": ""},
    })

    self.mainLogger.info ("Creating second character for domob...")
    self.moveWithPayment ("domob", {"nc": {"name": "foo"}}, CHARACTER_COST)
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob"},
      "eve": {"owner": ""},
      "foo": {"owner": "domob"},
    })

    self.mainLogger.info ("Testing Unicode names...")
    self.moveWithPayment (u"ß", {"nc": {"name": u"äöü"}}, CHARACTER_COST)
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "domob"},
      "eve": {"owner": ""},
      "foo": {"owner": "domob"},
      u"äöü": {"owner": u"ß"},
    })

    self.mainLogger.info ("Transfering a character...")
    charId = self.characterId ("adam")
    self.sendMove ("domob", {"c": {charId: {"send": "andy"}}})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "andy"},
      "eve": {"owner": ""},
      "foo": {"owner": "domob"},
      u"äöü": {"owner": u"ß"},
    })
    self.sendMove ("andy", {"c": {charId: {"send": ""}}})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": ""},
      "eve": {"owner": ""},
      "foo": {"owner": "domob"},
      u"äöü": {"owner": u"ß"},
    })

    self.mainLogger.info ("Non-owner cannot update the character...")
    self.sendMove ("domob", {"c": {charId: {"send": "domob"}}})
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
    self.moveWithPayment ("domob", {"nc": {"name": "alt"}}, CHARACTER_COST)
    self.generate (1)
    self.expectPartial ({
      "alt": {"owner": "domob"},
    })

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)



if __name__ == "__main__":
  CharactersTest ().main ()
