#!/usr/bin/env python
# coding=utf8

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

from pxtest import PXTest, CHARACTER_COST

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
    self.createCharacter ("adam", "r")
    self.sendMove ("", {"nc": {"name": "eve", "faction": "r"}})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "adam", "faction": "r"},
    })

    self.mainLogger.info ("Testing \"\" as owner name...")
    self.createCharacter ("", "g")
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "adam", "faction": "r"},
      "": {"owner": "", "faction": "g"},
    })

    self.mainLogger.info ("Creating second character for one owner...")
    self.createCharacter ("adam", "b")
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "adam", "faction": "r"},
      "adam 2": {"owner": "adam", "faction": "b"},
      "": {"owner": "", "faction": "g"},
    })

    self.mainLogger.info ("Testing Unicode owner...")
    self.createCharacter (u"äöü", "b")
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "adam"},
      "adam 2": {"owner": "adam"},
      "": {"owner": ""},
      u"äöü": {"owner": u"äöü"},
    })

    self.mainLogger.info ("Transfering a character...")
    c = self.getCharacters ()["adam"]
    c.sendMove ({"send": "andy"})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "adam", "faction": "b"},
      "andy": {"owner": "andy", "faction": "r"},
      "": {"owner": ""},
      u"äöü": {"owner": u"äöü"},
    })

    self.mainLogger.info ("Non-owner cannot update the character...")
    c = self.getCharacters ()["adam"]
    idStr = c.getIdStr ()
    self.sendMove ("domob", {"c": {idStr: {"send": "domob"}}})
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "adam"},
      "andy": {"owner": "andy"},
      "": {"owner": ""},
      u"äöü": {"owner": u"äöü"},
    })

    self.mainLogger.info ("Multiple creations in one transaction...")
    data = [
      {"faction": "r"},
      {"faction": "g"},
      {"faction": "b"},
    ]
    self.moveWithPayment ("domob", {"nc": data}, 2.5 * CHARACTER_COST)
    self.generate (1)
    self.expectPartial ({
      "adam": {"owner": "adam"},
      "andy": {"owner": "andy"},
      "": {"owner": ""},
      u"äöü": {"owner": u"äöü"},
      "domob": {"faction": "r"},
      "domob 2": {"faction": "g"},
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
    self.createCharacter ("domob", "b")
    self.generate (1)
    self.expectPartial ({
      "domob": {"owner": "domob"},
    })

    self.rpc.xaya.reconsiderblock (blk)
    self.expectGameState (originalState)



if __name__ == "__main__":
  CharactersTest ().main ()
