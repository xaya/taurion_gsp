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
Integration test for the on-chain jobs board, transport type.  Exercises the
full move path (moveprocessor "j" dispatch, block processing, the getjobs read
RPC and settlement) end-to-end: post -> accept -> deliver, plus cancel and the
accept-then-cancel-rejected case.  The per-op logic is covered exhaustively by
the C++ unit tests; this proves the real chain path and coin/inventory effects.
"""

from pxtest import PXTest


class JobsTransportTest (PXTest):

  def onlyJob (self):
    """Returns the single job on the board (asserting there is exactly one)."""
    jobs = self.getRpc ("getjobs")
    self.assertEqual (len (jobs), 1)
    return jobs[0]

  def newestJob (self):
    """Returns the most recently posted job on the board."""
    jobs = self.getRpc ("getjobs")
    assert len (jobs) > 0
    return max (jobs, key=lambda j: j["id"])

  def jobGone (self, jobId):
    """Returns whether the given job is no longer on the board."""
    return jobId not in [j["id"] for j in self.getRpc ("getjobs")]

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def reserved (self, name):
    return self.getAccounts ()[name].getBalance ("reserved")

  def run (self):
    self.mainLogger.info ("Setting up accounts and a destination building...")
    self.initAccount ("poster", "r")
    self.initAccount ("courier", "r")
    self.generate (1)
    self.giftCoins ({"poster": 1000000, "courier": 1000000})

    self.build ("checkmark", "poster", {"x": 0, "y": 0}, rot=0)
    self.buildingId = max (self.getBuildings ().keys ())
    self.assertEqual (self.getBuildings ()[self.buildingId].getOwner (),
                      "poster")

    self.testDeliver ()
    self.testCancel ()
    self.testProgressDump ()
    self.testHaul ()

    self.mainLogger.info ("Jobs transport integration test succeeded.")

  def testProgressDump (self):
    self.mainLogger.info ("Delivering bit-by-bit from character cargo...")
    self.sendMove ("poster", {"j": [{
      "t": "transport", "d": 86400, "r": 2000, "co": 0,
      "to": self.buildingId, "items": {"foo": 5},
    }]})
    self.generate (1)
    jobId = self.newestJob ()["id"]
    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)

    # Dock a courier character at the destination and load part of the
    # manifest into its cargo from the courier's building inventory.
    self.createCharacters ("courier")
    self.generate (1)
    self.moveCharactersTo ({"courier": {"x": 3, "y": 0}})
    cId = self.getCharacters ()["courier"].getId ()
    self.sendMove ("courier", {"c": {"id": cId, "eb": self.buildingId}})
    self.generate (1)
    assert self.getCharacters ()["courier"].isInBuilding ()
    self.dropIntoBuilding (self.buildingId, "courier", {"foo": 3})
    self.sendMove ("courier", {"c": {"id": cId, "pu": {"f": {"foo": 3}}}})
    self.generate (1)

    # First dump: 3 of 5 land with the poster, the job records progress and
    # nothing is paid yet.
    posterBefore = self.getBuildings ()[self.buildingId] \
        .getFungibleInventory ("poster").get ("foo", 0)
    reservedBefore = self.reserved ("poster")
    self.sendMove ("courier", {"j": [{"f": jobId, "ch": cId}]})
    self.generate (1)
    job = self.newestJob ()
    self.assertEqual (job["id"], jobId)
    self.assertEqual (dict (job["items"]), {"foo": 2})
    self.assertEqual (self.reserved ("poster"), reservedBefore)
    self.assertEqual (
        self.getBuildings ()[self.buildingId]
            .getFungibleInventory ("poster").get ("foo", 0),
        posterBefore + 3)

    # Second trip completes the manifest and settles the reward.
    self.dropIntoBuilding (self.buildingId, "courier", {"foo": 2})
    self.sendMove ("courier", {"c": {"id": cId, "pu": {"f": {"foo": 2}}}})
    self.generate (1)
    before = self.available ("courier")
    self.sendMove ("courier", {"j": [{"f": jobId, "ch": cId}]})
    self.generate (1)
    assert self.jobGone (jobId)
    self.assertEqual (self.available ("courier"), before + 2000)

  def testHaul (self):
    self.mainLogger.info ("Testing a haul (poster-supplied goods)...")
    self.build ("checkmark", "poster", {"x": 50, "y": 0}, rot=0)
    destId = max (self.getBuildings ().keys ())
    srcId = self.buildingId

    self.dropIntoBuilding (srcId, "poster", {"bar": 7})
    self.sendMove ("poster", {"j": [{
      "t": "haul", "d": 86400, "r": 1500, "co": 4000,
      "from": srcId, "to": destId, "items": {"bar": 7},
    }]})
    self.generate (1)
    job = self.newestJob ()
    jobId = job["id"]
    self.assertEqual (job["type"], "haul")
    self.assertEqual (job["from"], srcId)
    self.assertEqual (job["to"], destId)
    # The goods are reserved by the job: out of the poster's inventory.
    self.assertEqual (
        self.getBuildings ()[srcId].getFungibleInventory ("poster")
            .get ("bar", 0), 0)

    self.mainLogger.info ("Accepting hands the goods to the worker...")
    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (
        self.getBuildings ()[srcId].getFungibleInventory ("courier"),
        {"bar": 7})

    # The courier hauls them over (staged into their inventory at the
    # destination) and fulfils all-at-once.
    self.dropIntoBuilding (destId, "courier", {"bar": 7})
    before = self.available ("courier")
    self.sendMove ("courier", {"j": [{"f": jobId}]})
    self.generate (1)
    assert self.jobGone (jobId)
    self.assertEqual (
        self.getBuildings ()[destId].getFungibleInventory ("poster"),
        {"bar": 7})
    # Reward + collateral back.
    self.assertEqual (self.available ("courier"), before + 1500 + 4000)

  def testDeliver (self):
    self.mainLogger.info ("Posting a transport job...")
    self.sendMove ("poster", {"j": [{
      "t": "transport", "d": 86400, "r": 2000, "co": 8000,
      "to": self.buildingId, "items": {"foo": 5},
    }]})
    self.generate (1)

    job = self.onlyJob ()
    jobId = job["id"]
    self.assertEqual (job["state"], "open")
    self.assertEqual (job["poster"], "poster")
    self.assertEqual (job["reward"], 2000)
    self.assertEqual (job["building"], self.buildingId)
    self.assertEqual (dict (job["items"]), {"foo": 5})
    # Reward 2000 escrowed + fee max(1, 2000*100/10000 = 20) = 20 burned.
    self.assertEqual (self.available ("poster"), 1000000 - 2000 - 20)
    self.assertEqual (self.reserved ("poster"), 2000)

    self.mainLogger.info ("Accepting the job...")
    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)
    job = self.onlyJob ()
    self.assertEqual (job["state"], "accepted")
    self.assertEqual (job["worker"], "courier")
    self.assertEqual (self.available ("courier"), 1000000 - 8000)
    self.assertEqual (self.reserved ("courier"), 8000)

    self.mainLogger.info ("Delivering from the worker's own inventory at B...")
    # Stage the goods over what would be one or more trips, then fulfil once.
    self.dropIntoBuilding (self.buildingId, "courier", {"foo": 5})
    self.sendMove ("courier", {"j": [{"f": jobId}]})
    self.generate (1)

    self.assertEqual (self.getRpc ("getjobs"), [])
    b = self.getBuildings ()[self.buildingId]
    self.assertEqual (b.getFungibleInventory ("poster"), {"foo": 5})
    self.assertEqual (b.getFungibleInventory ("courier"), {})
    # Worker paid the reward and got the collateral back; no reserve left.
    self.assertEqual (self.available ("courier"),
                      1000000 - 8000 + 2000 + 8000)
    self.assertEqual (self.reserved ("courier"), 0)

  def testCancel (self):
    self.mainLogger.info ("Testing cancel and accept-then-cancel-rejected...")
    post = {"j": [{
      "t": "transport", "d": 86400, "r": 1000, "co": 500,
      "to": self.buildingId, "items": {"foo": 1},
    }]}

    self.sendMove ("poster", post)
    self.generate (1)
    jobId = self.onlyJob ()["id"]
    before = self.available ("poster")
    self.assertEqual (self.reserved ("poster"), 1000)

    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    self.assertEqual (self.getRpc ("getjobs"), [])
    self.assertEqual (self.available ("poster"), before + 1000)
    self.assertEqual (self.reserved ("poster"), 0)

    # Repost, have the courier accept, then the poster's cancel is rejected.
    self.sendMove ("poster", post)
    self.generate (1)
    jobId = self.onlyJob ()["id"]
    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (self.onlyJob ()["state"], "accepted")

    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    self.assertEqual (self.onlyJob ()["state"], "accepted")


if __name__ == "__main__":
  JobsTransportTest ().main ()
