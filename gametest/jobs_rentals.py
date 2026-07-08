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
as approval, and a toll refunded when the traveller dies in real combat.
"""

from pxtest import PXTest


class JobsRentalsTest (PXTest):

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def reserved (self, name):
    return self.getAccounts ()[name].getBalance ("reserved")

  def newestJob (self):
    jobs = self.getRpc ("getjobs")
    assert len (jobs) > 0
    return max (jobs, key=lambda j: j["id"])

  def jobGone (self, jobId):
    return jobId not in [j["id"] for j in self.getRpc ("getjobs")]

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

    self.mainLogger.info ("Jobs rentals integration test succeeded.")

  def testRental (self):
    self.mainLogger.info ("Renting items: post, handover, return...")
    self.dropIntoBuilding (self.buildingId, "owner", {"foo": 5})

    self.sendMove ("renter", {"j": [{
      "t": "rental", "d": 86400, "r": 1000, "co": 0, "rent": 300,
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
    self.sendMove ("owner", {"j": [{"a": job["id"]}]})
    self.generate (1)
    self.assertEqual (self.newestJob ()["state"], "accepted")
    self.assertEqual (self.reserved ("advertiser"), 500)

  def testToll (self):
    self.mainLogger.info ("Paying a toll, then the traveller dies...")
    self.createCharacters ("renter")
    self.createCharacters ("gatekeeper")
    self.generate (1)
    traveller = self.getCharacters ()["renter"].getId ()

    self.sendMove ("renter", {"j": [{
      "t": "toll", "d": 86400, "r": 400, "co": 0,
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


if __name__ == "__main__":
  JobsRentalsTest ().main ()
