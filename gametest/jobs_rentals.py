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
Integration test for the payer/payee-swapped rental family on the real chain
path: a rental's handover-on-accept and renter-fulfilled return with the
rent/deposit split, an ad-slot rented cross-faction with the owner's accept
as approval (plus slot exclusivity, booking a future window ahead and the
sale-void), a toll refunded when the traveller dies in real combat, and the
rental grace gap: a legal player transfer landing between the deadline
passing (on an ordinary block) and the expiry sweep observing it.
"""

from pxtest import PXTest


class JobsRentalsTest (PXTest):

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def reserved (self, name):
    return self.getAccounts ()[name].getBalance ("reserved")

  def run (self):
    self.mainLogger.info ("Setting up accounts and a building...")
    self.initAccount ("owner", "r")
    self.initAccount ("renter", "r")
    self.initAccount ("advertiser", "g")
    self.initAccount ("gatekeeper", "g")
    self.generate (1)
    self.giftCoins ({"owner": 1000000, "renter": 1000000,
                     "advertiser": 1000000})

    self.build ("checkmark", "owner", {"x": 0, "y": 0}, rot=0)
    self.buildingId = max (self.getBuildings ().keys ())

    self.testRental ()
    self.testAdSlot ()
    self.testToll ()
    self.testRentalGraceGap ()

    self.mainLogger.info ("Jobs rentals integration test succeeded.")

  def testRental (self):
    self.mainLogger.info ("Renting items: post, handover, return...")
    self.dropIntoBuilding (self.buildingId, "owner", {"foo": 5})

    self.sendMove ("renter", {"j": [{
      "t": "rental", "d": 86400, "wd": 86400, "r": 1000, "co": 0, "rent": 300,
      "i": "foo", "n": 5, "b": self.buildingId, "w": "owner",
    }]})
    self.generate (1)
    job = self.newestJob ()
    jobId = job["id"]
    self.assertEqual (job["type"], "rental")
    self.assertEqual (job["designated"], "owner")
    self.assertEqual (self.reserved ("renter"), 1000)

    self.mainLogger.info ("The lessor's accept hands the goods over...")
    self.sendMove ("owner", {"j": [{"a": jobId}]})
    self.generate (1)
    b = self.getBuildings ()[self.buildingId]
    self.assertEqual (b.getFungibleInventory ("owner"), {})
    self.assertEqual (b.getFungibleInventory ("renter"), {"foo": 5})

    self.mainLogger.info ("The renter returns and the escrow splits...")
    ownerBefore = self.available ("owner")
    renterBefore = self.available ("renter")
    self.sendMove ("renter", {"j": [{"f": jobId}]})
    self.generate (1)
    assert self.jobGone (jobId)
    b = self.getBuildings ()[self.buildingId]
    self.assertEqual (b.getFungibleInventory ("owner"), {"foo": 5})
    self.assertEqual (b.getFungibleInventory ("renter"), {})
    # Rent (300) to the lessor; the deposit (700) back to the renter.
    self.assertEqual (self.available ("owner"), ownerBefore + 300)
    self.assertEqual (self.available ("renter"), renterBefore + 700)
    self.assertEqual (self.reserved ("renter"), 0)

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

  def testToll (self):
    self.mainLogger.info ("Paying a toll, then the traveller dies...")
    self.createCharacters ("renter")
    self.createCharacters ("gatekeeper")
    self.generate (1)
    traveller = self.getCharacters ()["renter"].getId ()

    self.sendMove ("renter", {"j": [{
      "t": "toll", "d": 86400, "wd": 86400, "r": 400, "co": 0,
      "ch": traveller, "w": "gatekeeper",
    }]})
    self.generate (1)
    job = self.newestJob ()
    jobId = job["id"]
    self.assertEqual (job["type"], "toll")
    self.assertEqual (job["character"], traveller)

    self.sendMove ("gatekeeper", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (self.newestJob ()["state"], "accepted")

    # The traveller dies inside the window (killed by the gatekeeper, even):
    # the toll refunds to the poster.
    renterBefore = self.available ("renter")
    keeperBefore = self.available ("gatekeeper")
    self.changeCharacterVehicle ("gatekeeper", "light attacker")
    self.moveCharactersTo ({
      "renter": {"x": 50, "y": 50},
      "gatekeeper": {"x": 50, "y": 50},
    })
    self.setCharactersHP ({"renter": {"a": 1, "s": 0}})
    self.generate (1)
    assert "renter" not in self.getCharacters ()

    assert self.jobGone (jobId)
    self.assertEqual (self.available ("renter"), renterBefore + 400)
    self.assertEqual (self.available ("gatekeeper"), keeperBefore)

  def testRentalGraceGap (self):
    self.mainLogger.info ("Rental grace: a real redeposit lands in the "
                          "overdue gap before the sweep...")
    b = self.getBuildings ()[self.buildingId]
    # The lessor still holds the goods returned by the first rental.
    self.assertEqual (b.getFungibleInventory ("owner"), {"foo": 5})

    self.sendMove ("renter", {"j": [{
      "t": "rental", "d": 86400, "wd": 86400, "r": 1000, "co": 0, "rent": 300,
      "i": "foo", "n": 5, "b": self.buildingId, "w": "owner",
    }]})
    self.generate (1)
    jobId = self.newestJob ()["id"]
    self.sendMove ("owner", {"j": [{"a": jobId}]})
    self.generate (1)
    # Accepting starts the work clock: read the (rewritten, accept-relative)
    # deadline only now.
    deadline = self.newestJob ()["deadline"]

    self.mainLogger.info ("The renter parks the goods with a third party...")
    self.sendMove ("renter", {"x": [{
      "b": self.buildingId, "i": "foo", "n": 5, "t": "advertiser",
    }]})
    self.generate (1)
    b = self.getBuildings ()[self.buildingId]
    self.assertEqual (b.getFungibleInventory ("renter"), {})

    # Thread the deadline into a superblock window exactly like the
    # transport gating test: a superblock just inside it, then an ordinary
    # block carrying the overdue gap.
    sbSecs = self.roConfig ().params.superblock_seconds
    assert sbSecs >= 4, "test window needs superblock_seconds >= 4"
    lastSb = deadline - 2
    self.env.setMockTime (lastSb)
    self.generate (1, superblocks=False)
    assert not self.jobGone (jobId)

    # One ORDINARY block past the deadline (gap 3 < superblock_seconds)
    # carries both gap moves: a legal player transfer returns the goods,
    # and the renter's explicit fulfil is rejected (terms frozen once due).
    ownerBefore = self.available ("owner")
    renterBefore = self.available ("renter")
    self.sendMove ("advertiser", {"x": [{
      "b": self.buildingId, "i": "foo", "n": 5, "t": "renter",
    }]})
    self.sendMove ("renter", {"j": [{"f": jobId}]})
    self.env.setMockTime (deadline + 1)
    self.generate (1, superblocks=False)
    b = self.getBuildings ()[self.buildingId]
    self.assertEqual (b.getFungibleInventory ("renter"), {"foo": 5})
    assert not self.jobGone (jobId)
    self.assertEqual (self.reserved ("renter"), 1000)

    # The next superblock's sweep observes the redeposited goods and
    # settles as a clean return: rent to the lessor, deposit refunded.
    self.env.setMockTime (lastSb + sbSecs)
    self.generate (1, superblocks=False)
    assert self.jobGone (jobId)
    self.assertEqual (self.historyOutcome (jobId), "completed")
    b = self.getBuildings ()[self.buildingId]
    self.assertEqual (b.getFungibleInventory ("owner"), {"foo": 5})
    self.assertEqual (b.getFungibleInventory ("renter"), {})
    self.assertEqual (self.available ("owner"), ownerBefore + 300)
    self.assertEqual (self.available ("renter"), renterBefore + 700)


if __name__ == "__main__":
  JobsRentalsTest ().main ()
