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
Runs tests about the basic handling of characters (creating them, transferring
them and retrieving them through RPC).
"""

from pxtest import PXTest

import time


def sleepSome ():
  """
  Sleep for a short amount of time, which should be enough for the pending
  moves to be processed in the GSP.
  """

  time.sleep (0.1)


class PendingTest (PXTest):

  def run (self):
    self.generate (101);

    self.mainLogger.info ("Creating test character...")
    self.createCharacter ("domob", "r")
    self.generate (1)
    # getGameState ensures that we sync up at least for the confirmed state
    # before we look at the pending state.
    self.getGameState ()
    self.assertEqual (self.getPendingState (), {
      "characters": [],
      "newcharacters": {},
    })

    self.mainLogger.info ("Performing pending updates...")
    self.createCharacter ("domob", "g")
    c = self.getCharacters ()["domob"]
    c.sendMove ({"wp": []})

    sleepSome ()
    self.assertEqual (self.getPendingState (), {
      "characters":
        [
          {
            "id": c.getId (),
            "waypoints": [],
          }
        ],
      "newcharacters":
        {
          "domob": [{"faction": "g"}],
        },
    })

    self.createCharacter ("domob", "b")
    self.createCharacter ("andy", "r")
    c.sendMove ({"wp": [{"x": 5, "y": -5}]})

    sleepSome ()
    oldPending = self.getPendingState ()
    self.assertEqual (oldPending, {
      "characters":
        [
          {
            "id": c.getId (),
            "waypoints": [{"x": 5, "y": -5}],
          }
        ],
      "newcharacters":
        {
          "domob": [{"faction": "g"}, {"faction": "b"}],
          "andy": [{"faction": "r"}],
        },
    })

    self.mainLogger.info ("Confirming the moves...")
    self.generate (1)
    self.getGameState ()
    self.assertEqual (self.getPendingState (), {
      "characters": [],
      "newcharacters": {},
    })

    self.mainLogger.info ("Unconfirming the moves...")
    blk = self.rpc.xaya.getbestblockhash ()
    self.rpc.xaya.invalidateblock (blk)
    sleepSome ()
    self.assertEqual (self.getPendingState (), oldPending)


if __name__ == "__main__":
  PendingTest ().main ()
