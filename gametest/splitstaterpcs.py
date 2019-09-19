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

from pxtest import PXTest

"""
Tests how the full game state corresponds to the "split state" RPCs
like getaccounts or getregions.
"""


class SplitStateRpcsTest (PXTest):

  def run (self):
    self.collectPremine ()

    # Set up a non-trivial situation, where we have characters, prospected
    # regions and kills/fame.
    self.createCharacter ("prospector", "r")
    self.createCharacter ("killed", "g")
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

    # Test that the full game state corresponds to the split RPCs.
    state = self.getGameState ()
    accounts = self.getRpc ("getaccounts")
    characters = self.getRpc ("getcharacters")
    regions = self.getRpc ("getregions")
    prizes = self.getRpc ("getprizestats")
    assert len (accounts) > 0
    assert len (characters) > 0
    assert len (regions) > 0
    assert len (prizes) > 0
    self.assertEqual (accounts, state["accounts"])
    self.assertEqual (characters, state["characters"])
    self.assertEqual (regions, state["regions"])
    self.assertEqual (prizes, state["prizes"])


if __name__ == "__main__":
  SplitStateRpcsTest ().main ()
