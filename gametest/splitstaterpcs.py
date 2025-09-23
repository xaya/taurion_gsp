#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2025  Autonomous Worlds Ltd
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
Tests how the full game state corresponds to the "split state" RPCs
like getaccounts or getregions.
"""

from pxtest import PXTest


class SplitStateRpcsTest (PXTest):

  def run (self):
    # Set up a non-trivial situation, where we have characters, prospected
    # regions, kills/fame, buildings, ground loot and ongoing operations.
    self.initAccount ("prospector", "r")
    self.initAccount ("killed", "g")
    self.sendMove ("sale buyer", {"vc": {"m": {}}}, burn=10)
    self.dropLoot ({"x": 1, "y": 2}, {"foo": 5, "bar": 10})
    self.dropLoot ({"x": -1, "y": 20}, {"foo": 5})
    self.build ("checkmark", None, {"x": -100, "y": 200}, rot=5)
    self.createCharacters ("prospector")
    self.createCharacters ("killed")
    self.generate (1)
    self.moveCharactersTo ({
      "prospector": {"x": 0, "y": 0},
      "killed": {"x": 0, "y": 0},
    })
    self.setCharactersHP ({
      "killed": {"a": 1, "s": 0},
    })
    self.getCharacters ()["prospector"].sendMove ({"prospect": {}})
    self.generate (20)
    self.moveCharactersTo ({
      "prospector": {"x": 100, "y": -100},
    })
    self.getCharacters ()["prospector"].sendMove ({"prospect": {}})
    self.generate (3)

    # Test that the full game state corresponds to the split RPCs.
    state = self.getGameState ()
    accounts = self.getRpc ("getaccounts")
    buildings = self.getRpc ("getbuildings")
    characters = self.getRpc ("getcharacters")
    loot = self.getRpc ("getgroundloot")
    ongoings = self.getRpc ("getongoings")
    regions = self.getRpc ("getregions", fromheight=0)
    moneySupply = self.getRpc ("getmoneysupply")
    prizes = self.getRpc ("getprizestats")
    assert len (accounts) > 0
    assert len (buildings) > 0
    assert len (characters) > 0
    assert len (loot) > 0
    assert len (ongoings) > 0
    assert len (regions) > 0
    assert len (moneySupply["entries"]) > 0
    assert len (prizes) > 0
    self.assertEqual (accounts, state["accounts"])
    self.assertEqual (buildings, state["buildings"])
    self.assertEqual (characters, state["characters"])
    self.assertEqual (loot, state["groundloot"])
    self.assertEqual (ongoings, state["ongoings"])
    self.assertEqual (regions, state["regions"])
    self.assertEqual (moneySupply, state["moneysupply"])
    self.assertEqual (prizes, state["prizes"])

    # Test the bootstrap data.
    self.assertEqual (self.getRpc ("getbootstrapdata"), {
      "regions": regions,
    })


if __name__ == "__main__":
  SplitStateRpcsTest ().main ()
