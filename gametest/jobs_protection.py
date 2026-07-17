#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020-2021  Autonomous Worlds Ltd
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
Integration test for the protection family on the real chain path: the
approval-required accept flow (assign + designated accept), a bodyguard
failing through an actual combat kill of the protected character (the
character entity hook + forfeit-to-poster), and an escort settled by a
single-moment fulfil once the protectee docks at the destination.
"""

from pxtest import PXTest


class JobsProtectionTest (PXTest):

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def run (self):
    self.mainLogger.info ("Setting up accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("guard", "r")
    self.initAccount ("outsider", "r")
    self.initAccount ("enemy", "g")
    self.generate (1)
    self.giftCoins ({"poster": 1000000, "guard": 1000000})

    self.testBodyguard ()
    self.testEscort ()

    self.mainLogger.info ("Jobs protection integration test succeeded.")

  def testBodyguard (self):
    self.mainLogger.info ("Posting a bodyguard job on the poster's char...")
    self.createCharacters ("poster")
    self.createCharacters ("enemy")
    self.generate (1)
    protectee = self.getCharacters ()["poster"].getId ()

    self.sendMove ("poster", {"j": [{
      "t": "bodyguard", "d": 86400, "wd": 86400, "r": 1000, "co": 500, "ch": protectee,
    }]})
    self.generate (1)
    job = self.newestJob ()
    jobId = job["id"]
    self.assertEqual (job["type"], "bodyguard")
    self.assertEqual (job["character"], protectee)

    self.mainLogger.info ("Approval flow: accept requires designation...")
    self.sendMove ("guard", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (self.newestJob ()["state"], "open")

    self.sendMove ("poster", {"j": [{"s": jobId, "w": "guard"}]})
    self.generate (1)
    # A non-designated worker still cannot accept.
    self.sendMove ("outsider", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (self.newestJob ()["state"], "open")
    self.sendMove ("guard", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (self.newestJob ()["state"], "accepted")

    self.mainLogger.info ("The protectee dies in combat: forfeit...")
    self.changeCharacterVehicle ("enemy", "light attacker")
    posterBefore = self.available ("poster")
    guardBefore = self.available ("guard")
    self.moveCharactersTo ({
      "poster": {"x": 0, "y": 0},
      "enemy": {"x": 0, "y": 0},
    })
    self.setCharactersHP ({"poster": {"a": 1, "s": 0}})
    self.generate (1)
    assert "poster" not in self.getCharacters ()

    assert self.jobGone (jobId)
    # Reward refunds and the guard's bond forfeits, both to the poster.
    self.assertEqual (self.available ("poster"), posterBefore + 1000 + 500)
    self.assertEqual (self.available ("guard"), guardBefore)
    stats = self.getAccounts ()["guard"].data["jobstats"]
    self.assertEqual (stats["failed"], 1)
    # The forfeit also marks the poster's record (the scam-vetting signal:
    # posters can force forfeits by killing their own protectee).
    self.assertEqual (
        self.getAccounts ()["poster"].data["jobstats"]["posterfailed"], 1)

  def testEscort (self):
    self.mainLogger.info ("Escorting the poster's char to a building...")
    self.build ("checkmark", "poster", {"x": 100, "y": 0}, rot=0)
    destId = max (self.getBuildings ().keys ())

    self.createCharacters ("poster")
    self.generate (1)
    protectee = self.getCharacters ()["poster"].getId ()

    self.sendMove ("poster", {"j": [{
      "t": "escort", "d": 86400, "wd": 86400, "r": 1000, "co": 500,
      "ch": protectee, "to": destId,
    }]})
    self.generate (1)
    jobId = self.newestJob ()["id"]
    self.sendMove ("poster", {"j": [{"s": jobId, "w": "guard"}]})
    self.generate (1)
    self.sendMove ("guard", {"j": [{"a": jobId}]})
    self.generate (1)

    self.mainLogger.info ("Fulfil is rejected until the protectee arrives...")
    self.sendMove ("guard", {"j": [{"f": jobId}]})
    self.generate (1)
    assert not self.jobGone (jobId)

    # The protectee reaches and enters the destination.
    self.moveCharactersTo ({"poster": {"x": 103, "y": 0}})
    self.sendMove ("poster", {"c": {"id": protectee, "eb": destId}})
    self.generate (1)
    assert self.getCharacters ()["poster"].isInBuilding ()

    before = self.available ("guard")
    self.sendMove ("guard", {"j": [{"f": jobId}]})
    self.generate (1)
    assert self.jobGone (jobId)
    self.assertEqual (self.available ("guard"), before + 1000 + 500)


if __name__ == "__main__":
  JobsProtectionTest ().main ()
