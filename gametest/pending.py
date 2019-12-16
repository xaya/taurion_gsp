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
Tests the tracking of pending moves.
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
    self.collectPremine ()

    # Some well-defined position that we use, which also reflects the
    # region that will be prospected.  We use a different position and
    # region for mining.
    positionProspect = {"x": 0, "y": 0}
    regionProspect = self.rpc.game.getregionat (coord=positionProspect)["id"]
    positionMining = {"x": 100, "y": -100}
    regionMining = self.rpc.game.getregionat (coord=positionMining)["id"]

    self.mainLogger.info ("Creating test character...")
    self.initAccount ("andy", "b")
    self.initAccount ("domob", "r")
    self.initAccount ("miner", "g")
    self.createCharacters ("domob")
    self.createCharacters ("miner")
    self.generate (1)
    self.moveCharactersTo ({
      "domob": positionProspect,
      "miner": positionMining,
    })
    self.getCharacters ()["miner"].sendMove ({"prospect": {}})
    self.generate (15)
    # getnullstate ensures that we sync up at least for the confirmed state
    # before we look at the pending state.
    self.getRpc ("getnullstate")
    self.assertEqual (self.getPendingState (), {
      "characters": [],
      "newcharacters": [],
    })

    self.mainLogger.info ("Performing pending updates...")
    self.createCharacters ("domob")
    c1 = self.getCharacters ()["domob"]
    c1.sendMove ({"wp": []})

    sleepSome ()
    self.assertEqual (self.getPendingState (), {
      "characters":
        [
          {
            "id": c1.getId (),
            "waypoints": [],
            "drop": False,
            "pickup": False,
          }
        ],
      "newcharacters":
        [
          {"name": "domob", "creations": [{"faction": "r"}]},
        ],
    })

    self.createCharacters ("domob")
    self.createCharacters ("andy")
    c1.sendMove ({"wp": [{"x": 5, "y": -5}]})
    c1.sendMove ({"pu": {"f": {"foo": 2}}})

    sleepSome ()
    self.assertEqual (self.getPendingState (), {
      "characters":
        [
          {
            "id": c1.getId (),
            "waypoints": [{"x": 5, "y": -5}],
            "drop": False,
            "pickup": True,
          }
        ],
      "newcharacters":
        [
          {"name": "andy", "creations": [{"faction": "b"}]},
          {"name": "domob", "creations": [{"faction": "r"}] * 2},
        ],
    })

    c1.sendMove ({"prospect": {}})
    c1.sendMove ({"drop": {"f": {"foo": 2}}})
    c2 = self.getCharacters ()["miner"]
    c2.sendMove ({"mine": {}})
    sleepSome ()
    oldPending = self.getPendingState ()
    self.assertEqual (oldPending, {
      "characters":
        [
          {
            "id": c1.getId (),
            "drop": True,
            "pickup": True,
            "prospecting": regionProspect,
          },
          {
            "id": c2.getId (),
            "drop": False,
            "pickup": False,
            "mining": regionMining,
          },
        ],
      "newcharacters":
        [
          {"name": "andy", "creations": [{"faction": "b"}]},
          {"name": "domob", "creations": [{"faction": "r"}, {"faction": "r"}]},
        ],
    })

    self.mainLogger.info ("Confirming the moves...")
    self.generate (1)
    self.getRpc ("getnullstate")
    self.assertEqual (self.getPendingState (), {
      "characters": [],
      "newcharacters": [],
    })

    self.mainLogger.info ("Unconfirming the moves...")
    blk = self.rpc.xaya.getbestblockhash ()
    self.rpc.xaya.invalidateblock (blk)
    sleepSome ()
    self.assertEqual (self.getPendingState (), oldPending)


if __name__ == "__main__":
  PendingTest ().main ()
