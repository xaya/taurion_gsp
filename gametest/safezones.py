#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020  Autonomous Worlds Ltd
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
Tests basic behaviour of the starter "safe zones".
"""

from pxtest import PXTest, offsetCoord


def relativePos (x, y):
  """
  Returns a coordinate offset by the given (x, y) relative to a test
  starter zone's centre.  The zone's radius is 10.
  """

  centre = {"x": -2042, "y": 100}
  return offsetCoord (centre, {"x": x, "y": y}, False)


class SafeZonesTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.mainLogger.info ("Creating test characters...")
    self.initAccount ("red", "r")
    self.initAccount ("green", "g")
    self.generate (1)
    self.createCharacters ("red")
    self.createCharacters ("green")
    self.generate (1)
    self.changeCharacterVehicle ("red", "light attacker")
    self.changeCharacterVehicle ("green", "light attacker")

    self.mainLogger.info ("Testing findpath...")
    def findpath (**kwargs):
      path = self.rpc.game.findpath (l1range=1000, wpdist=1000, **kwargs)
      return path["dist"]
    self.expectError (1, "no connection",
                      findpath, faction="g",
                      source=relativePos (-15, 0),
                      target=relativePos (-5, 0))
    self.assertEqual (findpath (faction="r",
                                source=relativePos (-15, 0),
                                target=relativePos (-5, 0)),
                      4000 + 6 * (1000 // 3))
    assert findpath (faction="g", source=relativePos (-15, 0),
                     target=relativePos (15, 0)) > 30000
    assert findpath (faction="r", source=relativePos (-15, 11),
                     target=relativePos (15, 11)) < 30000

    self.mainLogger.info ("Testing movement...")
    # The green one has to move around the starter zone and will take longer
    # than a straight line (speed is 2).  The red one can take a "detour"
    # through the area to be faster.
    self.moveCharactersTo ({
      "red": relativePos (-15, 11),
      "green": relativePos (-15, 0),
    })
    chars = self.getCharacters ()
    chars["red"].sendMove ({"wp": [relativePos (15, 11)]})
    chars["green"].sendMove ({"wp": [relativePos (15, 0)]})
    self.generate (9)
    c = self.getCharacters ()["red"]
    self.assertEqual (c.isMoving (), False)
    self.assertEqual (c.getPosition (), relativePos (15, 11))
    self.generate (2)
    self.assertEqual (self.getCharacters ()["green"].isMoving (), True)

    self.mainLogger.info ("Testing combat...")
    self.moveCharactersTo ({
      "red": relativePos (0, 14),
      "green": relativePos (0, 11),
    })
    self.setCharactersHP ({
      "red": {"ma": 100, "a": 100, "ms": 100, "s": 100},
      "green": {"ma": 100, "a": 100, "ms": 100, "s": 100},
    })
    self.getCharacters ()["red"].sendMove ({"wp": [relativePos (0, 7)]})
    self.generate (2)
    c = self.getCharacters ()["red"]
    assert c.data["combat"]["hp"]["current"]["shield"] < 100
    assert "target" not in c.data["combat"]
    self.assertEqual (c.getPosition (), relativePos (0, 7))

    # Let the shield regenerate in the safe zone.
    self.generate (100)
    c = self.getCharacters ()["red"]
    self.assertEqual (c.data["combat"]["hp"]["current"]["shield"], 100)

    # Move out of the safe zone.  After the first block, we are still in
    # and there should not be any combat.  After that, we are out and combat
    # will resume.
    c.sendMove ({"wp": [relativePos (0, 12)], "speed": 1000})
    self.generate (1)
    chars = self.getCharacters ()
    c = chars["red"]
    self.assertEqual (c.getPosition (), relativePos (0, 10))
    self.assertEqual (c.data["combat"]["hp"]["current"]["shield"], 100)
    assert "target" not in c.data["combat"]
    c = chars["green"]
    self.assertEqual (c.data["combat"]["hp"]["current"]["shield"], 100)
    assert "target" not in c.data["combat"]

    # After the first block moving out of the safe zone, both characters
    # should have targeted each other (but not yet attacked).
    self.generate (1)
    chars = self.getCharacters ()
    c = chars["red"]
    self.assertEqual (c.data["combat"]["hp"]["current"]["shield"], 100)
    assert "target" in c.data["combat"]
    c = chars["green"]
    self.assertEqual (c.data["combat"]["hp"]["current"]["shield"], 100)
    assert "target" in c.data["combat"]

    # After the second block, also both attacks should have dealt some damage.
    self.generate (1)
    chars = self.getCharacters ()
    c = chars["red"]
    assert c.data["combat"]["hp"]["current"]["shield"] < 100
    assert "target" in c.data["combat"]
    c = chars["green"]
    assert c.data["combat"]["hp"]["current"]["shield"] < 100
    assert "target" in c.data["combat"]


if __name__ == "__main__":
  SafeZonesTest ().main ()
