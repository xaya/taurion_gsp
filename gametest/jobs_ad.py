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
Integration test for the ad-slot job type on the real chain path: an ad slot
rented cross-faction with the building owner's accept standing in as content
approval, slot exclusivity (no double-booking), booking a non-overlapping
future window ahead, and the sale-void that refunds every ad on a building
when it changes hands.  The per-op logic is covered by the C++ AdTests; this
proves the real chain path and the coin/reserve effects.
"""

from pxtest import PXTest


class JobsAdTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up accounts and a building...")
    self.initAccount ("owner", "r")
    self.initAccount ("renter", "r")
    self.initAccount ("advertiser", "g")
    self.generate (1)
    # Lower the minimum-reward floors (roconfig defaults) the same way an admin
    # would: the suite's rewards predate the floors, which are exercised in
    # jobs_caps.py.
    self.adminCommand ({"param": [
      {"n": "min-job-reward", "v": 1},
    ]})
    self.generate (1)
    self.giftCoins ({"owner": 1000000, "advertiser": 1000000})

    self.build ("checkmark", "owner", {"x": 0, "y": 0}, rot=0)
    self.buildingId = max (self.getBuildings ().keys ())

    self.testAdSlot ()

    self.mainLogger.info ("Jobs ad-slot integration test succeeded.")

  def testAdSlot (self):
    self.mainLogger.info ("Renting an ad slot cross-faction...")
    self.sendMove ("advertiser", {"j": [{
      "t": "ad", "d": 86400, "r": 500, "co": 0,
      "b": self.buildingId, "slot": 1, "hash": "deadbeef",
    }]})
    self.generate (1)
    job = self.newestJob ()
    self.assertEqual (job["type"], "ad")
    self.assertEqual (job["designated"], "owner")
    self.assertEqual (job["hash"], "deadbeef")
    assert "faction" not in job

    # The owner's accept is the content approval; the rent stays escrowed
    # until the period ends.
    adId = job["id"]
    self.sendMove ("owner", {"j": [{"a": adId}]})
    self.generate (1)
    self.assertEqual (self.newestJob ()["state"], "accepted")
    self.assertEqual (self.reserved ("advertiser"), 500)

    self.mainLogger.info ("The slot cannot be double-booked...")
    self.sendMove ("advertiser", {"j": [{
      "t": "ad", "d": 86400, "r": 300, "co": 0,
      "b": self.buildingId, "slot": 1, "hash": "beef",
    }]})
    self.generate (1)
    clashId = self.newestJob ()["id"]
    self.sendMove ("owner", {"j": [{"a": clashId}]})
    self.generate (1)
    clash = [j for j in self.getJobs () if j["id"] == clashId][0]
    self.assertEqual (clash["state"], "open")

    self.mainLogger.info ("...but a non-overlapping future window can be...")
    self.sendMove ("advertiser", {"j": [{
      "t": "ad", "d": 259200, "r": 300, "co": 0,
      "b": self.buildingId, "slot": 1, "hash": "f00d", "start": 172800,
    }]})
    self.generate (1)
    future = self.newestJob ()
    futureId = future["id"]
    assert "start" in future
    self.sendMove ("owner", {"j": [{"a": futureId}]})
    self.generate (1)
    self.assertEqual (self.newestJob ()["state"], "accepted")

    self.mainLogger.info ("Selling the building voids all its ads...")
    self.sendMove ("owner", {"b": {"id": self.buildingId, "send": "renter"}})
    self.generate (1)
    assert self.jobGone (adId)
    assert self.jobGone (clashId)
    assert self.jobGone (futureId)
    self.assertEqual (self.reserved ("advertiser"), 0)


if __name__ == "__main__":
  JobsAdTest ().main ()
