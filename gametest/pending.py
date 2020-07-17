#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2020  Autonomous Worlds Ltd
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

from pxtest import PXTest, offsetCoord

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

    positionBuilding = {"x": 200, "y": -500}

    self.mainLogger.info ("Creating test characters...")
    self.initAccount ("andy", "b")
    self.initAccount ("domob", "r")
    self.initAccount ("miner", "g")
    self.initAccount ("inbuilding", "b");
    self.createCharacters ("domob")
    self.createCharacters ("miner")
    self.createCharacters ("inbuilding", 2)
    self.generate (1)

    self.giftCoins ({"domob": 100})
    self.giftCoins ({"andy": 100})
    self.moveCharactersTo ({
      "domob": positionProspect,
      "miner": positionMining,
      "inbuilding": offsetCoord (positionBuilding, {"x": 30, "y": 0}, False),
      "inbuilding 2": offsetCoord (positionBuilding, {"x": -30, "y": 0}, False),
    })

    self.build ("ancient1", None, positionBuilding, 0)
    building = list (self.getBuildings ().keys ())[-1]
    self.dropIntoBuilding (building, "andy", {"foo": 100, "test ore": 10})
    self.getCharacters ()["inbuilding"].sendMove ({"eb": building})

    self.getCharacters ()["miner"].sendMove ({"prospect": {}})

    self.generate (15)
    self.syncGame ()
    self.assertEqual (self.getPendingState (), {
      "characters": [],
      "newcharacters": [],
      "accounts": [],
    })

    self.mainLogger.info ("Performing pending updates...")
    self.createCharacters ("domob")
    c1 = self.getCharacters ()["domob"]
    c1.sendMove ({"wp": None})

    sleepSome ()
    self.assertEqual (self.getPendingState (), {
      "characters":
        [
          {
            "id": c1.getId (),
            "waypoints": [],
            "drop": False,
            "pickup": False,
          },
        ],
      "newcharacters":
        [
          {"name": "domob", "creations": [{"faction": "r"}]},
        ],
      "accounts": [],
    })

    self.createCharacters ("domob")
    self.createCharacters ("andy")
    c1.sendMove ({"wp": self.rpc.game.encodewaypoints (wp=[{"x": 5, "y": -5}])})
    c1.sendMove ({"pu": {"f": {"foo": 2}}})

    cb1 = self.getCharacters ()["inbuilding"]
    cb1.sendMove ({"xb": {}})
    cb2 = self.getCharacters ()["inbuilding 2"]
    cb2.sendMove ({"eb": building})

    sleepSome ()
    self.assertEqual (self.getPendingState (), {
      "characters":
        [
          {
            "id": c1.getId (),
            "waypoints": [{"x": 5, "y": -5}],
            "drop": False,
            "pickup": True,
          },
          {
            "id": cb1.getId (),
            "exitbuilding": {"building": building},
            "pickup": False,
            "drop": False,
          },
          {
            "id": cb2.getId (),
            "enterbuilding": building,
            "pickup": False,
            "drop": False,
          },
        ],
      "newcharacters":
        [
          {"name": "andy", "creations": [{"faction": "b"}]},
          {"name": "domob", "creations": [{"faction": "r"}] * 2},
        ],
      "accounts": [],
    })

    c1.sendMove ({"prospect": {}})
    c1.sendMove ({"drop": {"f": {"foo": 2}}})
    c2 = self.getCharacters ()["miner"]
    c2.sendMove ({"mine": {}})
    self.getCharacters ()["inbuilding 2"].sendMove ({"eb": None})

    self.sendMove ("domob", {
      "vc": {"b": 10, "t": {"miner": 20}},
    })
    self.sendMove ("andy", {
      "s": [{"b": building, "t": "ref", "i": "test ore", "n": 9}],
    })

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
          {
            "id": cb1.getId (),
            "exitbuilding": {"building": building},
            "pickup": False,
            "drop": False,
          },
          {
            "id": cb2.getId (),
            "enterbuilding": None,
            "pickup": False,
            "drop": False,
          },
        ],
      "newcharacters":
        [
          {"name": "andy", "creations": [{"faction": "b"}]},
          {"name": "domob", "creations": [{"faction": "r"}, {"faction": "r"}]},
        ],
      "accounts":
        [
          {
            "name": "andy",
            "serviceops":
              [
                {
                  "building": building,
                  "type": "refining",
                  "cost": {"base": 30, "fee": 0},
                  "input": {"test ore": 9},
                  "output": {"bar": 6, "zerospace": 3},
                }
              ],
          },
          {
            "name": "domob",
            "coinops": {"burnt": 10, "transfers": {"miner": 20}},
          },
        ],
    })

    self.mainLogger.info ("Confirming the moves...")
    self.generate (1)
    self.syncGame ()
    self.assertEqual (self.getPendingState (), {
      "characters": [],
      "newcharacters": [],
      "accounts": [],
    })

    self.mainLogger.info ("Unconfirming the moves...")
    blk = self.rpc.xaya.getbestblockhash ()
    self.rpc.xaya.invalidateblock (blk)
    sleepSome ()
    self.assertEqual (self.getPendingState (), oldPending)
    self.generate (50)

    self.testDynObstacles ()

  def testDynObstacles (self):
    """
    Tests pending "found building" moves, which in particular also
    verifies the DynObstacles instance used for pending moves.
    """

    self.mainLogger.info ("Testing dynamic obstacles...")

    pos1 = {"x": 100, "y": 0}
    pos2 = offsetCoord (pos1, {"x": -1, "y": 0}, False)

    self.moveCharactersTo ({
      "domob": pos1,
      "andy": pos2,
    })
    self.dropLoot (pos1, {"foo": 10})
    self.dropLoot (pos2, {"foo": 10})
    c = self.getCharacters ()
    c["domob"].sendMove ({"pu": {"f": {"foo": 100}}})
    c["andy"].sendMove ({"pu": {"f": {"foo": 100}}})
    self.generate (1)

    # domob's huesli can be built, while andy's checkmark would overlap
    # with the dynamic obstacle presented by the domob character.
    c = self.getCharacters ()
    c["domob"].sendMove ({"fb": {"t": "huesli", "rot": 3}})
    c["andy"].sendMove ({"fb": {"t": "checkmark", "rot": 0}})
    self.assertEqual (self.getPendingState (), {
      "characters":
        [
          {
            "id": c["domob"].getId (),
            "foundbuilding":
              {
                "type": "huesli",
                "rotationsteps": 3,
              },
            "pickup": False,
            "drop": False,
          }
        ],
      "newcharacters": [],
      "accounts": [],
    })

    # Make sure nothing sticks around for later tests.
    self.generate (1)


if __name__ == "__main__":
  PendingTest ().main ()
