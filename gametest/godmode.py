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
Tests the god-mode commands.
"""

from pxtest import PXTest


class GodModeTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)
    c = self.getCharacters ()["domob"]
    charIdStr = c.getIdStr ()

    self.mainLogger.info ("Testing build...")
    self.build ("checkmark", None, {"x": 100, "y": 150}, rot=2)
    self.build ("checkmark", "domob", {"x": -100, "y": -150}, rot=0)
    buildings = self.getBuildings ()
    buildingId = list (buildings.values ())[0].getId ()
    self.assertEqual (buildings[1002].data, {
      "id": 1002,
      "type": "checkmark",
      "faction": "a",
      "centre": {"x": 100, "y": 150},
      "rotationsteps": 2,
      "servicefee": 0,
      "tiles":
        [
          {"x": 100, "y": 150},
          {"x": 100, "y": 149},
          {"x": 101, "y": 149},
          {"x": 102, "y": 148},
        ],
      "combat":
        {
          "hp":
            {
              "current": {"armour": 0, "shield": 0},
              "max": {"armour": 0, "shield": 0},
              "regeneration": {"armour": 0.0, "shield": 0.0},
            },
        },
      "inventories": {},
    })
    self.assertEqual (buildings[1003].data, {
      "id": 1003,
      "type": "checkmark",
      "faction": "r",
      "owner": "domob",
      "centre": {"x": -100, "y": -150},
      "rotationsteps": 0,
      "servicefee": 0,
      "tiles":
        [
          {"x": -100, "y": -150},
          {"x": -99, "y": -150},
          {"x": -100, "y": -149},
          {"x": -100, "y": -148},
        ],
      "combat":
        {
          "hp":
            {
              "current": {"armour": 0, "shield": 0},
              "max": {"armour": 0, "shield": 0},
              "regeneration": {"armour": 0.0, "shield": 0.0},
            },
        },
      "inventories": {},
    })

    self.mainLogger.info ("Testing teleport...")
    target = {"x": 28, "y": 9}
    self.adminCommand ({"god": {"teleport": {charIdStr: target}}})
    self.generate (1)
    self.assertEqual (self.getCharacters ()["domob"].getPosition (), target)

    self.mainLogger.info ("Testing sethp for characters...")
    self.adminCommand ({
      "god":
        {
          "sethp":
            {
              "c":
                {
                  charIdStr: {"a": 32, "s": 15, "ma": 100, "ms": 90},
                },
            },
        },
    })
    self.generate (1)
    hp = self.getCharacters ()["domob"].data["combat"]["hp"]
    self.assertEqual (hp["current"], {"armour": 32, "shield": 15})
    self.assertEqual (hp["max"], {"armour": 100, "shield": 90})

    self.mainLogger.info ("Testing sethp for buildings...")
    self.adminCommand ({
      "god":
        {
          "sethp":
            {
              "b":
                {
                  ("%s" % buildingId): {"a": 32, "s": 15},
                },
            },
        },
    })
    self.generate (1)
    hp = self.getBuildings ()[buildingId].data["combat"]["hp"]
    self.assertEqual (hp["current"], {"armour": 32, "shield": 15})
    self.assertEqual (hp["max"], {"armour": 0, "shield": 0})

    self.mainLogger.info ("Testing drop loot...")
    self.dropLoot ({"x": 1, "y": 2}, {"foo": 1, "bar": 2})
    self.dropIntoBuilding (buildingId, "domob", {"bar": 42})
    self.assertEqual (self.getRpc ("getgroundloot"), [
      {
        "position": {"x": 1, "y": 2},
        "inventory":
          {
            "fungible": {"bar": 2, "foo": 1},
          },
      },
    ])
    b = self.getBuildings ()[buildingId]
    self.assertEqual (b.getFungibleInventory ("domob"), {"bar": 42})

    self.mainLogger.info ("Testing gift coins...")
    self.giftCoins ({"daniel": 20, "andy": 42})
    self.giftCoins ({"daniel": 30})
    self.giftCoins ({"rich": 99000000000})
    acc = self.getAccounts ()
    self.assertEqual (acc["andy"].getBalance (), 42)
    self.assertEqual (acc["daniel"].getBalance (), 50)
    self.assertEqual (acc["daniel"].getFaction (), None)
    self.assertEqual (acc["rich"].getBalance (), 99000000000)
    

if __name__ == "__main__":
  GodModeTest ().main ()
