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
Tests that Taurion works fine with multiple name updates in a single block.
"""

from pxtest import PXTest, offsetCoord


class MultiUpdateTest (PXTest):

  def run (self):
    self.mainLogger.info ("Creating test character...")
    self.initAccount ("domob", "r")
    self.createCharacters ("domob")
    self.generate (1)

    # Start off from a known good location to make sure all is fine and
    # not flaky depending on the randomised spawn position.
    self.offset = {"x": -1377, "y": 1263}
    self.moveCharactersTo ({"domob": self.offset})

    self.mainLogger.info ("Multiple movement commands in a block...")
    c = self.getCharacters ()["domob"]
    c.moveTowards (offsetCoord ({"x": 10, "y": 0}, self.offset, False))
    c.moveTowards (offsetCoord ({"x": 0, "y": 10}, self.offset, False))
    self.generate (1)
    c = self.getCharacters ()["domob"]
    expected = {"x": 0, "y": c.getSpeed () // 1000}
    self.assertEqual (offsetCoord (c.getPosition (), self.offset, True),
                      expected)


if __name__ == "__main__":
  MultiUpdateTest ().main ()
